#include "JoltShapes.hpp"

#include "JoltMaterial.hpp"
#include "JoltUtils.hpp"

#include <Snowstorm/Assets/AssetManagerSingleton.hpp>
#include <Snowstorm/Components/HierarchyComponent.hpp>
#include <Snowstorm/Components/MeshComponent.hpp>
#include <Snowstorm/Components/PhysicsComponents.hpp>
#include <Snowstorm/Core/Application.hpp>
#include <Snowstorm/Core/Log.hpp>
#include <Snowstorm/Math/Transform.hpp>
#include <Snowstorm/Render/MeshLibrary.hpp>
#include <Snowstorm/World/World.hpp>

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include <functional>
#include <string>
#include <vector>

namespace Snowstorm
{
	namespace
	{
		struct Hasher
		{
			uint64_t H = 1469598103934665603ull;
			template <typename T>
			void Add(const T& v)
			{
				const auto* p = reinterpret_cast<const unsigned char*>(&v);
				for (size_t i = 0; i < sizeof(T); ++i)
				{
					H ^= p[i];
					H *= 1099511628211ull;
				}
			}
		};

		struct Part
		{
			JPH::RefConst<JPH::Shape> Shape;
			glm::vec3 Position;
			glm::quat Rotation;
		};

		void ParseSubmeshPath(const std::string& registryPath, std::string& outFile, int& outSubmesh)
		{
			const size_t q = registryPath.find("?submesh=");
			outFile = q == std::string::npos ? registryPath : registryPath.substr(0, q);
			outSubmesh = q == std::string::npos ? -1 : std::atoi(registryPath.c_str() + q + 9);
		}

		// Cook a render mesh into a collision shape (Hazel MeshCookingFactory, inline: the cooked vertex
		// blob is the MeshLibrary's cook cache, so this is a blob read + a Jolt build).
		JPH::RefConst<JPH::Shape> CookMeshShape(World& world, const AssetHandle handle, const uint32_t submeshIndex, const bool convex, const JPH::PhysicsMaterial* material)
		{
			if (!Application::Exists())
			{
				return nullptr;
			}
			auto& assets = world.GetSingleton<AssetManagerSingleton>();
			const AssetMetadata* meta = assets.GetMetadata(handle);
			if (!meta)
			{
				return nullptr;
			}
			std::string file;
			int submesh = -1;
			ParseSubmeshPath(meta->Path.generic_string(), file, submesh);
			if (submesh < 0)
			{
				submesh = static_cast<int>(submeshIndex);
			}
			auto& meshLib = Application::Get().GetServiceManager().GetService<MeshLibrary>();
			const auto cooked = meshLib.LoadCookedCPU(assets.Registry().Resolve(file).string(), submesh, handle, assets.Registry().SourceKey(handle));
			if (!cooked || cooked->Vertices.empty())
			{
				SS_CORE_WARN("MeshCollider: no geometry for '{}'.", meta->Path.string());
				return nullptr;
			}

			JPH::Shape::ShapeResult result;
			if (convex)
			{
				JPH::Array<JPH::Vec3> points;
				points.reserve(cooked->Vertices.size());
				for (const Vertex& v : cooked->Vertices)
				{
					points.push_back(JoltUtils::ToJoltVector(v.Position));
				}
				result = JPH::ConvexHullShapeSettings(points.data(), static_cast<int>(points.size()), JPH::cDefaultConvexRadius, material).Create();
			}
			else
			{
				JPH::VertexList vertices;
				vertices.reserve(cooked->Vertices.size());
				for (const Vertex& v : cooked->Vertices)
				{
					vertices.push_back(JPH::Float3(v.Position.x, v.Position.y, v.Position.z));
				}
				JPH::IndexedTriangleList triangles;
				triangles.reserve(cooked->Indices.size() / 3);
				for (size_t i = 0; i + 2 < cooked->Indices.size(); i += 3)
				{
					triangles.emplace_back(cooked->Indices[i], cooked->Indices[i + 1], cooked->Indices[i + 2], 0u);
				}
				JPH::PhysicsMaterialList materials;
				materials.push_back(material);
				result = JPH::MeshShapeSettings(std::move(vertices), std::move(triangles), std::move(materials)).Create();
			}
			if (result.HasError())
			{
				SS_CORE_WARN("MeshCollider: shape build failed for '{}': {}", meta->Path.string(), result.GetError().c_str());
				return nullptr;
			}
			return result.Get();
		}

		// Body-space pose of a collider entity (identity for the body itself).
		void RelativeToBody(World& world, const Entity body, const Entity collider, glm::vec3& outPos, glm::quat& outRot)
		{
			if (body == collider)
			{
				outPos = glm::vec3(0.0f);
				outRot = glm::quat(1, 0, 0, 0);
				return;
			}
			glm::vec3 scale;
			DecomposeTRS(glm::inverse(world.ComputeWorldMatrix(body)) * world.ComputeWorldMatrix(collider), outPos, outRot, scale);
		}
	}

	namespace
	{
		JPH::RefConst<JPH::Shape> BuildOrHash(const Entity body, const bool bodyIsStatic, const bool build, uint64_t& outAuthoredHash);
	}

	JPH::RefConst<JPH::Shape> JoltShapes::BuildBodyShape(const Entity body, const bool bodyIsStatic, uint64_t& outAuthoredHash)
	{
		return BuildOrHash(body, bodyIsStatic, true, outAuthoredHash);
	}

	uint64_t JoltShapes::ComputeAuthoredHash(const Entity body)
	{
		uint64_t hash = 0;
		(void)BuildOrHash(body, true, false, hash);
		return hash;
	}

	namespace
	{
	JPH::RefConst<JPH::Shape> BuildOrHash(const Entity body, const bool bodyIsStatic, const bool build, uint64_t& outAuthoredHash)
	{
		World& world = *body.GetWorld();
		auto& reg = world.GetRegistry();
		Hasher h;

		glm::vec3 bodyPos, bodyScale;
		glm::quat bodyRot;
		DecomposeTRS(world.ComputeWorldMatrix(body), bodyPos, bodyRot, bodyScale);
		h.Add(bodyScale);

		std::vector<Part> parts;
		auto collect = [&](const Entity e)
		{
			glm::vec3 pos;
			glm::quat rot;
			RelativeToBody(world, body, e, pos, rot);
			h.Add(pos);
			h.Add(rot);

			if (const auto* box = reg.try_get_const<BoxColliderComponent>(e.Handle()))
			{
				h.Add(*box);
				if (build)
					parts.push_back({new JPH::BoxShape(JoltUtils::ToJoltVector(glm::max(box->HalfSize, glm::vec3(0.01f))), JPH::cDefaultConvexRadius, new JoltMaterial(box->Material)), pos + rot * box->Offset, rot});
			}
			if (const auto* sphere = reg.try_get_const<SphereColliderComponent>(e.Handle()))
			{
				h.Add(*sphere);
				if (build)
					parts.push_back({new JPH::SphereShape(std::max(sphere->Radius, 0.01f), new JoltMaterial(sphere->Material)), pos + rot * sphere->Offset, rot});
			}
			if (const auto* capsule = reg.try_get_const<CapsuleColliderComponent>(e.Handle()))
			{
				h.Add(*capsule);
				if (build)
					parts.push_back({new JPH::CapsuleShape(std::max(capsule->HalfHeight, 0.0f), std::max(capsule->Radius, 0.01f), new JoltMaterial(capsule->Material)), pos + rot * capsule->Offset, rot});
			}
			if (const auto* mesh = reg.try_get_const<MeshColliderComponent>(e.Handle()))
			{
				h.Add(*mesh);
				AssetHandle handle = mesh->ColliderAsset;
				if (handle.Value() == 0)
				{
					if (const auto* mc = reg.try_get_const<MeshComponent>(e.Handle()))
					{
						handle = mc->Mesh;
					}
				}
				h.Add(handle);
				if (!build)
				{
					return;
				}
				// Default complexity: exact triangles for static bodies, a convex hull for moving ones.
				bool convex = !bodyIsStatic;
				if (mesh->CollisionComplexity == ECollisionComplexity::UseComplexAsSimple)
					convex = false;
				else if (mesh->CollisionComplexity == ECollisionComplexity::UseSimpleAsComplex)
					convex = true;
				if (JPH::RefConst<JPH::Shape> shape = CookMeshShape(world, handle, mesh->SubmeshIndex, convex, new JoltMaterial(mesh->Material)))
				{
					parts.push_back({shape, pos, rot});
				}
			}
		};

		// The body's own colliders, then child entities without a RigidBody (their colliders belong to
		// this body). A CompoundColliderComponent can opt out of the static-children walk.
		bool includeChildren = true;
		if (const auto* compound = reg.try_get_const<CompoundColliderComponent>(body.Handle()))
		{
			h.Add(compound->IncludeStaticChildColliders);
			includeChildren = compound->IncludeStaticChildColliders;
		}
		std::function<void(Entity)> visit = [&](const Entity e)
		{
			collect(e);
			if (!includeChildren)
			{
				return;
			}
			world.ForEachChild(e, [&](const Entity child)
			                   {
				if (!child.HasComponent<RigidBodyComponent>())
				{
					visit(child);
				} });
		};
		visit(body);

		if (const auto* rb = reg.try_get_const<RigidBodyComponent>(body.Handle()))
		{
			h.Add(*rb);
		}
		outAuthoredHash = h.H;

		if (!build || parts.empty())
		{
			return nullptr;
		}

		JPH::RefConst<JPH::Shape> shape;
		if (parts.size() == 1 && glm::length(parts[0].Position) < 1e-6f && std::abs(parts[0].Rotation.w - 1.0f) < 1e-6f)
		{
			shape = parts[0].Shape;
		}
		else
		{
			JPH::StaticCompoundShapeSettings compound;
			for (const Part& p : parts)
			{
				compound.AddShape(JoltUtils::ToJoltVector(p.Position), JoltUtils::ToJoltQuat(p.Rotation), p.Shape);
			}
			const JPH::Shape::ShapeResult result = compound.Create();
			if (result.HasError())
			{
				SS_CORE_WARN("JoltShapes: compound shape failed: {}", result.GetError().c_str());
				return nullptr;
			}
			shape = result.Get();
		}

		const bool unitScale = std::abs(bodyScale.x - 1.0f) < 1e-4f && std::abs(bodyScale.y - 1.0f) < 1e-4f && std::abs(bodyScale.z - 1.0f) < 1e-4f;
		if (!unitScale)
		{
			const JPH::Shape::ShapeResult scaled = JPH::ScaledShapeSettings(shape, JoltUtils::ToJoltVector(bodyScale)).Create();
			if (scaled.HasError())
			{
				SS_CORE_WARN("JoltShapes: scaled shape failed: {}", scaled.GetError().c_str());
				return shape;
			}
			shape = scaled.Get();
		}
		return shape;
	}
	}
}
