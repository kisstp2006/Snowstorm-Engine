#include "JoltScene.hpp"

#include "JoltMaterial.hpp"
#include "JoltUtils.hpp"

#include <Snowstorm/Components/IDComponent.hpp>
#include <Snowstorm/Components/PhysicsComponents.hpp>
#include <Snowstorm/Core/Log.hpp>
#include <Snowstorm/Debug/DebugDrawSingleton.hpp>
#include <Snowstorm/Physics/PhysicsSystem.hpp>
#include <Snowstorm/Scripting/ScriptEvents.hpp>
#include <Snowstorm/World/World.hpp>

#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/PhysicsSettings.h>
#ifdef JPH_DEBUG_RENDERER
#include <Jolt/Renderer/DebugRendererSimple.h>
#endif

namespace Snowstorm
{
	namespace
	{
		constexpr JPH::uint kNumBodyMutexes = 0; // Jolt default
		constexpr JPH::uint kMaxBodyPairs = 4096;
		constexpr JPH::uint kMaxContactConstraints = 2048;
		constexpr size_t kTempAllocatorBytes = 16 * 1024 * 1024;
	}

	JoltScene::JoltScene(World* world)
	    : m_EntityWorld(world), m_ContactListener(*this)
	{
		const PhysicsSettings& settings = PhysicsSystem::GetSettings();
		m_TempAllocator = std::make_unique<JPH::TempAllocatorImpl>(static_cast<JPH::uint>(kTempAllocatorBytes));
		m_System = std::make_unique<JPH::PhysicsSystem>();
		m_System->Init(settings.MaxBodies, kNumBodyMutexes, kMaxBodyPairs, kMaxContactConstraints,
		               m_BroadPhaseLayerInterface, m_ObjectVsBroadPhaseFilter, m_ObjectLayerPairFilter);
		m_System->SetContactListener(&m_ContactListener);
		m_System->SetCombineFriction(JoltMaterial::CombineFriction);
		m_System->SetCombineRestitution(JoltMaterial::CombineRestitution);
		m_System->SetGravity(JoltUtils::ToJoltVector(settings.Gravity));

		JPH::PhysicsSettings joltSettings = m_System->GetPhysicsSettings();
		joltSettings.mNumPositionSteps = settings.PositionSolverIterations;
		joltSettings.mNumVelocitySteps = settings.VelocitySolverIterations;
		m_System->SetPhysicsSettings(joltSettings);
	}

	JoltScene::~JoltScene()
	{
		// Release every body NOW: runtime components (in the registry, destroyed after the singletons) may
		// still hold a Ref<JoltBody>, and a late destructor must not touch this scene.
		for (auto& [uuid, body] : m_RigidBodies)
		{
			body->Release();
		}
		m_RigidBodies.clear();
		m_BodyToEntity.clear();
		m_System->SetContactListener(nullptr);
	}

	// ---------------------------------------------------------------------------------------------------
	// Simulation
	// ---------------------------------------------------------------------------------------------------

	void JoltScene::Simulate(const float fixedDt, JPH::JobSystem& jobSystem)
	{
		{
			std::lock_guard lock(m_ContactEventsMutex);
			m_ContactEvents.clear();
		}
		m_PersistedContacts = 0;

		const JPH::EPhysicsUpdateError err = m_System->Update(fixedDt, 1, m_TempAllocator.get(), &jobSystem);
		if (err != JPH::EPhysicsUpdateError::None)
		{
			SS_CORE_WARN("JoltScene: PhysicsSystem::Update error {} (raise the body/pair/contact limits).", static_cast<int>(err));
		}

		FlushContactEvents();
	}

	glm::vec3 JoltScene::GetGravity() const { return JoltUtils::FromJoltVector(m_System->GetGravity()); }
	void JoltScene::SetGravity(const glm::vec3& gravity) { m_System->SetGravity(JoltUtils::ToJoltVector(gravity)); }

	// ---------------------------------------------------------------------------------------------------
	// Bodies
	// ---------------------------------------------------------------------------------------------------

	Ref<JoltBody> JoltScene::CreateBody(const Entity entity, const bool activate)
	{
		if (!entity || !entity.HasComponent<IDComponent>())
		{
			return nullptr;
		}
		const UUID id = entity.GetComponent<IDComponent>().Id;
		DestroyBody(entity);

		Ref<JoltBody> body = CreateRef<JoltBody>(*this, entity, activate);
		if (!body->IsValid())
		{
			return nullptr; // no colliders yet, or creation failed (logged)
		}
		m_RigidBodies[id] = body;
		m_BodyToEntity[body->GetBodyID().GetIndexAndSequenceNumber()] = id;
		return body;
	}

	void JoltScene::DestroyBody(const Entity entity)
	{
		if (!entity || !entity.HasComponent<IDComponent>())
		{
			return;
		}
		DestroyBodyByEntityID(entity.GetComponent<IDComponent>().Id);
	}

	void JoltScene::DestroyBodyByEntityID(const UUID entityID)
	{
		const auto it = m_RigidBodies.find(entityID);
		if (it == m_RigidBodies.end())
		{
			return;
		}
		m_BodyToEntity.erase(it->second->GetBodyID().GetIndexAndSequenceNumber());
		it->second->Release();
		m_RigidBodies.erase(it);
	}

	Ref<JoltBody> JoltScene::GetBody(const Entity entity) const
	{
		if (!entity || !entity.HasComponent<IDComponent>())
		{
			return nullptr;
		}
		return GetBodyByEntityID(entity.GetComponent<IDComponent>().Id);
	}

	Ref<JoltBody> JoltScene::GetBodyByEntityID(const UUID entityID) const
	{
		const auto it = m_RigidBodies.find(entityID);
		return it == m_RigidBodies.end() ? nullptr : it->second;
	}

	Entity JoltScene::GetEntityByBodyID(const JPH::BodyID bodyID) const
	{
		const auto it = m_BodyToEntity.find(bodyID.GetIndexAndSequenceNumber());
		return it == m_BodyToEntity.end() ? Entity{} : m_EntityWorld->FindEntityByUUID(it->second);
	}

	void JoltScene::SetBodyType(const Entity entity, const EBodyType bodyType)
	{
		const Ref<JoltBody> body = GetBody(entity);
		if (!body)
		{
			return;
		}
		m_System->GetBodyInterface().SetMotionType(body->GetBodyID(), JoltUtils::ToJoltMotionType(bodyType), JPH::EActivation::Activate);
	}

	void JoltScene::Teleport(const Entity entity, const glm::vec3& targetPosition, const glm::quat& targetRotation, const bool force)
	{
		const Ref<JoltBody> body = GetBody(entity);
		if (!body)
		{
			return;
		}
		// Velocities are kept across a teleport (Hazel semantics); `force` wakes a sleeping body.
		body->SetTransform(targetPosition, targetRotation, force || !body->IsDynamic());
	}

	// ---------------------------------------------------------------------------------------------------
	// Queries
	// ---------------------------------------------------------------------------------------------------

	bool JoltScene::CastRay(const RayCastInfo& info, SceneQueryHit& outHit) const
	{
		outHit.Clear();
		const glm::vec3 dir = glm::normalize(info.Direction) * info.MaxDistance;
		const JPH::RRayCast ray(JoltUtils::ToJoltVector(info.Origin), JoltUtils::ToJoltVector(dir));

		JPH::IgnoreMultipleBodiesFilter ignore;
		for (const UUID excluded : info.ExcludedEntities)
		{
			if (const Ref<JoltBody> body = GetBodyByEntityID(excluded))
			{
				ignore.IgnoreBody(body->GetBodyID());
			}
		}

		JPH::RayCastResult result;
		if (!m_System->GetNarrowPhaseQuery().CastRay(ray, result, {}, {}, ignore))
		{
			return false;
		}
		const auto idIt = m_BodyToEntity.find(result.mBodyID.GetIndexAndSequenceNumber());
		outHit.HitEntity = idIt == m_BodyToEntity.end() ? UUID{0} : idIt->second;
		outHit.Distance = result.mFraction * info.MaxDistance;
		outHit.Position = info.Origin + glm::normalize(info.Direction) * outHit.Distance;

		JPH::BodyLockRead lock(m_System->GetBodyLockInterface(), result.mBodyID);
		if (lock.Succeeded())
		{
			outHit.Normal = JoltUtils::FromJoltVector(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, ray.GetPointOnRay(result.mFraction)));
		}
		return true;
	}

	// ---------------------------------------------------------------------------------------------------
	// Contacts (worker threads -> main thread after the step)
	// ---------------------------------------------------------------------------------------------------

	void JoltScene::OnContactEvent(const ContactType type, const JPH::BodyID bodyA, const JPH::BodyID bodyB, const glm::vec3& point, const glm::vec3& normal)
	{
		std::lock_guard lock(m_ContactEventsMutex);
		m_ContactEvents.push_back({type, bodyA, bodyB, point, normal});
	}

	void JoltScene::FlushContactEvents()
	{
		std::vector<ContactEvent> events;
		{
			std::lock_guard lock(m_ContactEventsMutex);
			events.swap(m_ContactEvents);
		}
		m_ContactsLastStep = static_cast<uint32_t>(events.size()) + m_PersistedContacts.load();
		if (events.empty() || !m_EntityWorld->HasSingleton<ScriptEventQueue>())
		{
			return;
		}

		auto& queue = m_EntityWorld->GetSingleton<ScriptEventQueue>();
		for (const ContactEvent& ev : events)
		{
			const Entity a = GetEntityByBodyID(ev.BodyA);
			const Entity b = GetEntityByBodyID(ev.BodyB);
			ContactType type = ev.Type;
			if (type == ContactType::CollisionEnd)
			{
				// Decide trigger-vs-collision from the bodies we still know about (the listener couldn't).
				const Ref<JoltBody> ba = a ? GetBody(a) : nullptr;
				const Ref<JoltBody> bb = b ? GetBody(b) : nullptr;
				if ((ba && ba->IsTrigger()) || (bb && bb->IsTrigger()))
				{
					type = ContactType::TriggerEnd;
				}
			}

			ScriptEvent out;
			switch (type)
			{
			case ContactType::CollisionBegin:
				out.Type = ScriptEvent::Kind::CollisionEnter;
				break;
			case ContactType::CollisionEnd:
				out.Type = ScriptEvent::Kind::CollisionExit;
				break;
			case ContactType::TriggerBegin:
				out.Type = ScriptEvent::Kind::TriggerEnter;
				break;
			case ContactType::TriggerEnd:
				out.Type = ScriptEvent::Kind::TriggerExit;
				break;
			default:
				continue;
			}
			out.Contact.Point = ev.Point;
			out.Contact.Normal = ev.Normal;
			// Both sides receive the callback with the other as `other` (Unity semantics).
			if (a)
			{
				out.A = a.Handle();
				out.B = b ? b.Handle() : entt::null;
				queue.Push(out);
			}
			if (b)
			{
				out.A = b.Handle();
				out.B = a ? a.Handle() : entt::null;
				out.Contact.Normal = -ev.Normal;
				queue.Push(out);
			}
		}
	}

	// ---------------------------------------------------------------------------------------------------
	// Debug
	// ---------------------------------------------------------------------------------------------------

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
				m_Out.Line3D(JoltUtils::FromJoltVector(from), JoltUtils::FromJoltVector(to), color.mU32);
			}
			void DrawText3D(JPH::RVec3Arg, const std::string_view&, JPH::ColorArg, float) override {}

		private:
			DebugDrawSingleton& m_Out;
		};
	}
#endif

	void JoltScene::DrawDebug(DebugDrawSingleton& out)
	{
#ifdef JPH_DEBUG_RENDERER
		LineRenderer renderer(out); // registers itself as JPH::DebugRenderer::sInstance for its lifetime
		JPH::BodyManager::DrawSettings settings;
		settings.mDrawShape = true;
		settings.mDrawShapeWireframe = true;
		m_System->DrawBodies(settings, &renderer);
#else
		(void)out;
#endif
	}
}
