#include "PhysicsSystems.hpp"

#include "Components/PhysicsBodyRuntimeComponent.hpp"
#include "Components/PhysicsComponents.hpp"
#include "JoltJobSystem.hpp"
#include "PhysicsCVars.hpp"
#include "PhysicsWorldSingleton.hpp"

#include <Snowstorm/Assets/AssetManagerSingleton.hpp>
#include <Snowstorm/Components/HierarchyComponent.hpp>
#include <Snowstorm/Components/MeshComponent.hpp>
#include <Snowstorm/Components/TransformComponent.hpp>
#include <Snowstorm/Components/WorldTransformComponent.hpp>
#include <Snowstorm/Core/Log.hpp>
#include <Snowstorm/Debug/DebugDrawSingleton.hpp>
#include <Snowstorm/Math/Transform.hpp>
#include <Snowstorm/Render/MeshLibrary.hpp>
#include <Snowstorm/World/Entity.hpp>
#include <Snowstorm/World/SimulationStateSingleton.hpp>
#include <Snowstorm/Core/Application.hpp>
#include <Snowstorm/ECS/SystemManager.hpp>

#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#ifdef JPH_DEBUG_RENDERER
#include <Jolt/Renderer/DebugRendererSimple.h>
#endif

#include <cstring>
#include <functional>
#include <vector>

namespace Snowstorm
{
	namespace
	{
		// ---- glm <-> Jolt ----
		JPH::Vec3 ToJolt(const glm::vec3& v) { return {v.x, v.y, v.z}; }
		JPH::Quat ToJolt(const glm::quat& q) { return {q.x, q.y, q.z, q.w}; }
		glm::vec3 ToGlm(const JPH::Vec3 v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
		glm::quat ToGlm(const JPH::Quat q) { return {q.GetW(), q.GetX(), q.GetY(), q.GetZ()}; }

		// ---- authored hash: any change rebuilds the body ----
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

		bool NearlyOne(const glm::vec3& s)
		{
			return std::abs(s.x - 1.0f) < 1e-4f && std::abs(s.y - 1.0f) < 1e-4f && std::abs(s.z - 1.0f) < 1e-4f;
		}

		// Nearest ancestor (inclusive) that carries a RigidBodyComponent: the body a collider belongs to.
		entt::entity OwningBody(TrackedRegistry& reg, entt::entity e)
		{
			for (entt::entity cur = e; cur != entt::null;)
			{
				if (reg.any_of<RigidBodyComponent>(cur))
				{
					return cur;
				}
				const auto* h = reg.try_get_const<HierarchyComponent>(cur);
				cur = h ? h->Parent : entt::null;
			}
			return entt::null;
		}

		// Local (body-space) transform of a collider entity relative to its body entity.
		void LocalToBody(World& world, const entt::entity body, const entt::entity collider, glm::vec3& outPos, glm::quat& outRot)
		{
			if (body == collider)
			{
				outPos = glm::vec3(0.0f);
				outRot = glm::quat(1, 0, 0, 0);
				return;
			}
			const glm::mat4 rel = glm::inverse(world.ComputeWorldMatrix(Entity{body, &world})) * world.ComputeWorldMatrix(Entity{collider, &world});
			glm::vec3 scale;
			DecomposeTRS(rel, outPos, outRot, scale);
		}

		void ParseSubmesh(const std::string& registryPath, std::string& outFile, int& outSubmesh)
		{
			const size_t q = registryPath.find("?submesh=");
			outFile = q == std::string::npos ? registryPath : registryPath.substr(0, q);
			outSubmesh = q == std::string::npos ? -1 : std::atoi(registryPath.c_str() + q + 9);
		}

		JPH::RefConst<JPH::Shape> BuildMeshShape(World& world, const AssetHandle handle, const bool convex)
		{
			auto& assets = world.GetSingleton<AssetManagerSingleton>();
			const AssetMetadata* meta = assets.GetMetadata(handle);
			if (!meta || !Application::Exists())
			{
				return nullptr;
			}
			std::string file;
			int submesh = -1;
			ParseSubmesh(meta->Path.generic_string(), file, submesh);
			if (submesh < 0)
			{
				submesh = 0; // whole-file handle: the first part (a collision mesh is usually one part)
			}
			auto& meshLib = Application::Get().GetServiceManager().GetService<MeshLibrary>();
			const std::string full = assets.Registry().Resolve(file).string();
			const auto cooked = meshLib.LoadCookedCPU(full, submesh, handle, assets.Registry().SourceKey(handle));
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
					points.push_back(ToJolt(v.Position));
				}
				result = JPH::ConvexHullShapeSettings(points.data(), static_cast<int>(points.size())).Create();
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
				result = JPH::MeshShapeSettings(std::move(vertices), std::move(triangles)).Create();
			}
			if (result.HasError())
			{
				SS_CORE_WARN("MeshCollider: shape build failed for '{}': {}", meta->Path.string(), result.GetError().c_str());
				return nullptr;
			}
			return result.Get();
		}
	}

	// =================================================================================================
	// PhysicsBodySyncSystem
	// =================================================================================================

	JPH::RefConst<JPH::Shape> PhysicsBodySyncSystem::BuildShape(const entt::entity body, glm::vec3& outScale, uint64_t& hash) const
	{
		auto& reg = m_World->GetRegistry();
		Hasher h;

		// World scale of the body entity: Jolt shapes are unscaled, so bake it with a ScaledShape.
		glm::vec3 bodyPos, bodyScale;
		glm::quat bodyRot;
		DecomposeTRS(m_World->ComputeWorldMatrix(Entity{body, m_World}), bodyPos, bodyRot, bodyScale);
		outScale = bodyScale;
		h.Add(bodyScale);

		// Collect colliders on the body and on descendants without a body of their own.
		struct Part
		{
			JPH::RefConst<JPH::Shape> Shape;
			glm::vec3 Pos;
			glm::quat Rot;
		};
		std::vector<Part> parts;

		auto collect = [&](const entt::entity e)
		{
			glm::vec3 pos;
			glm::quat rot;
			LocalToBody(*m_World, body, e, pos, rot);
			h.Add(pos);
			h.Add(rot);

			if (const auto* box = reg.try_get_const<BoxColliderComponent>(e))
			{
				h.Add(*box);
				const glm::vec3 half = glm::max(box->HalfExtents, glm::vec3(0.01f));
				parts.push_back({new JPH::BoxShape(ToJolt(half)), pos + rot * box->Offset, rot});
			}
			if (const auto* sphere = reg.try_get_const<SphereColliderComponent>(e))
			{
				h.Add(*sphere);
				parts.push_back({new JPH::SphereShape(std::max(sphere->Radius, 0.01f)), pos + rot * sphere->Offset, rot});
			}
			if (const auto* capsule = reg.try_get_const<CapsuleColliderComponent>(e))
			{
				h.Add(*capsule);
				parts.push_back({new JPH::CapsuleShape(std::max(capsule->HalfHeight, 0.0f), std::max(capsule->Radius, 0.01f)), pos + rot * capsule->Offset, rot});
			}
			if (const auto* mesh = reg.try_get_const<MeshColliderComponent>(e))
			{
				h.Add(*mesh);
				AssetHandle handle = mesh->Mesh;
				if (handle.Value() == 0)
				{
					if (const auto* mc = reg.try_get_const<MeshComponent>(e))
					{
						handle = mc->Mesh;
					}
				}
				h.Add(handle);
				if (JPH::RefConst<JPH::Shape> shape = BuildMeshShape(*m_World, handle, mesh->Convex))
				{
					parts.push_back({shape, pos, rot});
				}
			}
		};

		std::function<void(entt::entity)> visit = [&](const entt::entity e)
		{
			collect(e);
			m_World->ForEachChild(Entity{e, m_World}, [&](const Entity child)
			                      {
				if (!reg.any_of<RigidBodyComponent>(child.Handle()))
				{
					visit(child.Handle());
				} });
		};
		visit(body);

		if (const auto* rb = reg.try_get_const<RigidBodyComponent>(body))
		{
			h.Add(*rb);
		}
		hash = h.H;

		if (parts.empty())
		{
			return nullptr;
		}

		JPH::RefConst<JPH::Shape> shape;
		if (parts.size() == 1 && glm::length(parts[0].Pos) < 1e-6f && std::abs(parts[0].Rot.w - 1.0f) < 1e-6f)
		{
			shape = parts[0].Shape;
		}
		else
		{
			JPH::StaticCompoundShapeSettings compound;
			for (const Part& p : parts)
			{
				compound.AddShape(ToJolt(p.Pos), ToJolt(p.Rot), p.Shape);
			}
			const JPH::Shape::ShapeResult result = compound.Create();
			if (result.HasError())
			{
				SS_CORE_WARN("Physics: compound shape failed: {}", result.GetError().c_str());
				return nullptr;
			}
			shape = result.Get();
		}

		if (!NearlyOne(bodyScale))
		{
			const JPH::Shape::ShapeResult scaled = JPH::ScaledShapeSettings(shape, ToJolt(bodyScale)).Create();
			if (scaled.HasError())
			{
				SS_CORE_WARN("Physics: scaled shape failed: {}", scaled.GetError().c_str());
				return shape;
			}
			shape = scaled.Get();
		}
		return shape;
	}

	void PhysicsBodySyncSystem::SyncEntity(PhysicsWorldSingleton& physics, const entt::entity e, const RigidBodyComponent* rb, const bool simulating)
	{
		auto& reg = m_World->GetRegistry();
		auto& bodies = physics.Bodies();

		glm::vec3 scale;
		uint64_t hash = 0;
		JPH::RefConst<JPH::Shape> shape;
		// The hash needs the collider walk anyway, so the shape is rebuilt speculatively and only kept when
		// the hash moved (cheap at thesis scene sizes: a handful of primitive shapes per body).
		shape = BuildShape(e, scale, hash);

		auto* rt = reg.any_of<PhysicsBodyRuntimeComponent>(e) ? &reg.get<PhysicsBodyRuntimeComponent>(e) : nullptr;
		if (rt && rt->AuthoredHash == hash)
		{
			// Unchanged authored data. Static/kinematic bodies follow an edited transform (the gizmo, a
			// script writing Position). Dynamic ones are driven by the simulation while it runs — but while
			// it is NOT running (Edit mode) the transform is the truth, so an edit teleports the body too;
			// otherwise Play would start from the stale pose and the edit would vanish.
			if ((!rt->Dynamic || !simulating) && reg.WasChanged<WorldTransformComponent>(e))
			{
				glm::vec3 pos, s;
				glm::quat rot;
				DecomposeTRS(reg.Read<WorldTransformComponent>(e).LocalToWorld, pos, rot, s);
				bodies.SetPositionAndRotationWhenChanged(rt->Body, ToJolt(pos), ToJolt(rot), JPH::EActivation::Activate);
			}
			return;
		}

		// (Re)build: drop the old body first.
		if (rt && !rt->Body.IsInvalid())
		{
			bodies.RemoveBody(rt->Body);
			bodies.DestroyBody(rt->Body);
			physics.UnbindBody(rt->Body);
			rt->Body = JPH::BodyID();
		}
		if (!shape)
		{
			if (rt)
			{
				reg.remove<PhysicsBodyRuntimeComponent>(e);
			}
			return; // a RigidBody with no collider anywhere: nothing to simulate yet
		}

		glm::vec3 pos, s;
		glm::quat rot;
		DecomposeTRS(reg.Read<WorldTransformComponent>(e).LocalToWorld, pos, rot, s);

		const MotionType motion = rb ? rb->Motion : MotionType::Static;
		JPH::EMotionType joltMotion = JPH::EMotionType::Static;
		if (motion == MotionType::Dynamic)
			joltMotion = JPH::EMotionType::Dynamic;
		else if (motion == MotionType::Kinematic)
			joltMotion = JPH::EMotionType::Kinematic;
		const bool moving = joltMotion != JPH::EMotionType::Static;

		JPH::BodyCreationSettings settings(shape, ToJolt(pos), ToJolt(rot), joltMotion, PhysicsLayers::Make(rb ? rb->CollisionLayer : 0u, moving));
		settings.mUserData = static_cast<JPH::uint64>(e);
		if (rb)
		{
			settings.mFriction = rb->Friction;
			settings.mRestitution = rb->Restitution;
			settings.mLinearDamping = rb->LinearDamping;
			settings.mAngularDamping = rb->AngularDamping;
			settings.mGravityFactor = rb->GravityFactor;
			settings.mIsSensor = rb->IsTrigger;
			if (joltMotion == JPH::EMotionType::Dynamic)
			{
				settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
				settings.mMassPropertiesOverride.mMass = std::max(rb->Mass, 0.001f);
			}
		}
		// Static bodies are added asleep; moving ones start active only while simulating (in Edit mode
		// they exist for debug draw and queries, and wake when Play starts).
		const JPH::BodyID id = bodies.CreateAndAddBody(settings, (moving && simulating) ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
		if (id.IsInvalid())
		{
			SS_CORE_ERROR("Physics: body creation failed for entity {} (body limit?).", static_cast<uint32_t>(e));
			return;
		}
		physics.BindBody(id, e);

		auto& runtime = reg.Ensure<PhysicsBodyRuntimeComponent>(e);
		runtime.Body = id;
		runtime.Shape = shape;
		runtime.AuthoredHash = hash;
		runtime.Dynamic = joltMotion == JPH::EMotionType::Dynamic;
		runtime.HasPrev = false;
	}

	void PhysicsBodySyncSystem::RemoveStale(PhysicsWorldSingleton& physics) const
	{
		auto& reg = m_World->GetRegistry();
		auto& bodies = physics.Bodies();
		std::vector<entt::entity> stale;
		for (const auto view = reg.view<PhysicsBodyRuntimeComponent>(); const entt::entity e : view)
		{
			const bool hasCollider = reg.any_of<BoxColliderComponent>(e) || reg.any_of<SphereColliderComponent>(e) ||
			                         reg.any_of<CapsuleColliderComponent>(e) || reg.any_of<MeshColliderComponent>(e);
			// A body stays while its entity has a RigidBody, or is a collider-only static with no body above.
			const bool stillABody = reg.any_of<RigidBodyComponent>(e) || (hasCollider && OwningBody(reg, e) == entt::null);
			if (!stillABody)
			{
				stale.push_back(e);
			}
		}
		for (const entt::entity e : stale)
		{
			auto& rt = reg.get<PhysicsBodyRuntimeComponent>(e);
			if (!rt.Body.IsInvalid())
			{
				bodies.RemoveBody(rt.Body);
				bodies.DestroyBody(rt.Body);
				physics.UnbindBody(rt.Body);
			}
			reg.remove<PhysicsBodyRuntimeComponent>(e);
		}
	}

	void PhysicsBodySyncSystem::Execute(Timestep)
	{
		if (!m_World->HasSingleton<PhysicsWorldSingleton>())
		{
			return;
		}
		auto& physics = SingletonView<PhysicsWorldSingleton>();
		auto& reg = m_World->GetRegistry();

		// Bodies whose entity died: Jolt must forget them before the ID is reused. The registry destroys
		// the runtime component with the entity, so we can only notice through the body map.
		physics.ForEachBoundBody([&](const JPH::BodyID body, const entt::entity e)
		                         {
			if (!reg.valid(e) || !reg.any_of<PhysicsBodyRuntimeComponent>(e))
			{
				physics.Bodies().RemoveBody(body);
				physics.Bodies().DestroyBody(body);
				return false; // unbind
			}
			return true; });

		RemoveStale(physics);

		bool simulating = true;
		if (m_World->HasSingleton<SimulationStateSingleton>())
		{
			simulating = m_World->GetSingleton<SimulationStateSingleton>().IsPlaying();
		}

		// Every RigidBody entity, plus collider-only entities that have no RigidBody above (static).
		std::vector<entt::entity> bodyEntities;
		for (const auto view = reg.view<RigidBodyComponent, WorldTransformComponent>(); const entt::entity e : view)
		{
			bodyEntities.push_back(e);
		}
		auto addStatic = [&](const entt::entity e)
		{
			if (!reg.any_of<RigidBodyComponent>(e) && reg.any_of<WorldTransformComponent>(e) && OwningBody(reg, e) == entt::null)
			{
				bodyEntities.push_back(e);
			}
		};
		for (const auto v = reg.view<BoxColliderComponent>(); const entt::entity e : v)
			addStatic(e);
		for (const auto v = reg.view<SphereColliderComponent>(); const entt::entity e : v)
			addStatic(e);
		for (const auto v = reg.view<CapsuleColliderComponent>(); const entt::entity e : v)
			addStatic(e);
		for (const auto v = reg.view<MeshColliderComponent>(); const entt::entity e : v)
			addStatic(e);

		for (const entt::entity e : bodyEntities)
		{
			SyncEntity(physics, e, reg.try_get_const<RigidBodyComponent>(e), simulating);
		}
	}

	// =================================================================================================
	// PhysicsStepSystem
	// =================================================================================================

	void PhysicsStepSystem::Execute(const Timestep ts)
	{
		if (!m_World->HasSingleton<PhysicsWorldSingleton>())
		{
			return;
		}
		auto& physics = SingletonView<PhysicsWorldSingleton>();
		auto& reg = m_World->GetRegistry();
		auto& bodies = physics.Bodies();
		const float dt = ts.GetSeconds();

		// Previous-pose snapshot (interpolation) + kinematic targets from the authored transforms.
		for (const auto view = reg.view<PhysicsBodyRuntimeComponent>(); const entt::entity e : view)
		{
			auto& rt = reg.get<PhysicsBodyRuntimeComponent>(e);
			if (rt.Body.IsInvalid())
			{
				continue;
			}
			if (rt.Dynamic)
			{
				JPH::RVec3 p;
				JPH::Quat q;
				bodies.GetPositionAndRotation(rt.Body, p, q);
				rt.PrevPosition = ToGlm(p);
				rt.PrevRotation = ToGlm(q);
				rt.HasPrev = true;
			}
			else if (bodies.GetMotionType(rt.Body) == JPH::EMotionType::Kinematic && reg.any_of<WorldTransformComponent>(e))
			{
				glm::vec3 pos, s;
				glm::quat rot;
				DecomposeTRS(reg.Read<WorldTransformComponent>(e).LocalToWorld, pos, rot, s);
				bodies.MoveKinematic(rt.Body, ToJolt(pos), ToJolt(rot), dt);
			}
		}

		// Bodies created in Edit mode were added asleep; wake the moving ones now that we simulate.
		for (const auto view = reg.view<PhysicsBodyRuntimeComponent>(); const entt::entity e : view)
		{
			const auto& rt = reg.Read<PhysicsBodyRuntimeComponent>(e);
			if (rt.Dynamic && !rt.HasPrev)
			{
				bodies.ActivateBody(rt.Body);
			}
		}

		physics.ResetStepStats();
		// The engine's job pool through the JoltJobSystem service; single-threaded when there is no
		// Application (unit tests, offline tools) — same results, Jolt is deterministic either way.
		JPH::JobSystem* jobs = nullptr;
		static JPH::JobSystemSingleThreaded s_SingleThreaded(JPH::cMaxPhysicsJobs);
		if (Application::Exists() && Application::Get().GetServiceManager().ServiceRegistered<JoltJobSystem>())
		{
			jobs = &ServiceView<JoltJobSystem>();
		}
		else
		{
			jobs = &s_SingleThreaded;
		}
		const JPH::EPhysicsUpdateError err = physics.System().Update(dt, 1, &physics.TempAllocator(), jobs);
		if (err != JPH::EPhysicsUpdateError::None)
		{
			SS_CORE_WARN("Physics: PhysicsSystem::Update reported error {} (raise the body/pair/contact limits).", static_cast<int>(err));
		}

		if (PhysicsCVars::LogStats.Get() && ++m_StepsSinceLog >= 60)
		{
			m_StepsSinceLog = 0;
			SS_CORE_INFO("Physics: bodies={} active={} contacts={} dt={:.4f}", physics.BodyCount(), physics.ActiveBodyCount(), physics.ContactCountLastStep(), dt);
		}
	}

	// =================================================================================================
	// PhysicsWriteBackSystem
	// =================================================================================================

	void PhysicsWriteBackSystem::Execute(Timestep)
	{
		if (!m_World->HasSingleton<PhysicsWorldSingleton>())
		{
			return;
		}
		auto& physics = SingletonView<PhysicsWorldSingleton>();
		auto& reg = m_World->GetRegistry();
		auto& bodies = physics.Bodies();
		const float alpha = m_World->GetSystemManager().FixedAlpha();

		for (const auto view = reg.view<PhysicsBodyRuntimeComponent, TransformComponent>(); const entt::entity e : view)
		{
			const auto& rt = reg.Read<PhysicsBodyRuntimeComponent>(e);
			if (!rt.Dynamic || rt.Body.IsInvalid() || !rt.HasPrev)
			{
				continue;
			}
			JPH::RVec3 p;
			JPH::Quat q;
			bodies.GetPositionAndRotation(rt.Body, p, q);
			glm::vec3 worldPos = ToGlm(p);
			glm::quat worldRot = ToGlm(q);

			const auto* rb = reg.try_get_const<RigidBodyComponent>(e);
			if (rb && rb->Interpolate)
			{
				worldPos = glm::mix(rt.PrevPosition, worldPos, alpha);
				worldRot = glm::slerp(rt.PrevRotation, worldRot, alpha);
			}

			// World pose -> the entity's LOCAL transform (through the parent's inverse), keeping scale.
			glm::mat4 world = glm::translate(glm::mat4(1.0f), worldPos) * glm::mat4_cast(worldRot);
			const Entity parent = m_World->GetParent(Entity{e, m_World});
			glm::mat4 local = parent ? glm::inverse(m_World->ComputeWorldMatrix(parent)) * world : world;
			glm::vec3 lp, ls;
			glm::quat lr;
			if (!DecomposeTRS(local, lp, lr, ls))
			{
				continue;
			}
			const TransformComponent& cur = reg.Read<TransformComponent>(e);
			if (glm::distance(cur.Position, lp) < 1e-6f && std::abs(glm::dot(cur.Rotation, lr)) > 1.0f - 1e-7f)
			{
				continue; // at rest: don't dirty the transform (keeps culling/TLAS quiet)
			}
			reg.patch<TransformComponent>(e, [&](TransformComponent& tr)
			                              {
				tr.Position = lp;
				tr.Rotation = lr; });
		}
	}

	// =================================================================================================
	// PhysicsDebugDrawSystem
	// =================================================================================================

#ifdef JPH_DEBUG_RENDERER
	namespace
	{
		class LineRenderer final : public JPH::DebugRendererSimple
		{
		public:
			explicit LineRenderer(DebugDrawSingleton& out)
			    : m_Out(out)
			{
			}
			void DrawLine(const JPH::RVec3Arg from, const JPH::RVec3Arg to, const JPH::ColorArg color) override
			{
				m_Out.Line3D(ToGlm(from), ToGlm(to), color.mU32);
			}
			void DrawText3D(JPH::RVec3Arg, const std::string_view&, JPH::ColorArg, float) override {}

		private:
			DebugDrawSingleton& m_Out;
		};
	}
#endif

	void PhysicsDebugDrawSystem::Execute(Timestep)
	{
		if (!m_World->HasSingleton<DebugDrawSingleton>())
		{
			return;
		}
		auto& out = SingletonView<DebugDrawSingleton>();
		out.Clear(); // the editor drew last frame's lines already (UI phase precedes PreRender)
#ifdef JPH_DEBUG_RENDERER
		if (!PhysicsCVars::DebugDraw.Get() || !m_World->HasSingleton<PhysicsWorldSingleton>())
		{
			return;
		}
		// DebugRendererSimple registers itself as the global JPH::DebugRenderer::sInstance for its lifetime.
		LineRenderer renderer(out);
		JPH::BodyManager::DrawSettings settings;
		settings.mDrawShape = true;
		settings.mDrawShapeWireframe = true;
		SingletonView<PhysicsWorldSingleton>().System().DrawBodies(settings, &renderer);
#endif
	}
}
