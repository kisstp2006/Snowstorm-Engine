#include "PhysicsComponents.hpp"

#include <Snowstorm/Components/ComponentRegistry.hpp>

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::enumeration<MotionType>("Snowstorm::MotionType")(
		    value("Static", MotionType::Static),
		    value("Kinematic", MotionType::Kinematic),
		    value("Dynamic", MotionType::Dynamic));

		registration::class_<RigidBodyComponent>("Snowstorm::RigidBodyComponent")
		    .constructor()
		    .property("Motion", &RigidBodyComponent::Motion)
		    .property("Mass", &RigidBodyComponent::Mass)(metadata("Min", 0.001f))
		    .property("Friction", &RigidBodyComponent::Friction)(metadata("Min", 0.0f), metadata("Max", 1.0f))
		    .property("Restitution", &RigidBodyComponent::Restitution)(metadata("Min", 0.0f), metadata("Max", 1.0f))
		    .property("LinearDamping", &RigidBodyComponent::LinearDamping)(metadata("Min", 0.0f))
		    .property("AngularDamping", &RigidBodyComponent::AngularDamping)(metadata("Min", 0.0f))
		    .property("GravityFactor", &RigidBodyComponent::GravityFactor)
		    .property("IsTrigger", &RigidBodyComponent::IsTrigger)
		    .property("CollisionLayer", &RigidBodyComponent::CollisionLayer)(metadata("Min", 0), metadata("Max", 31))
		    .property("Interpolate", &RigidBodyComponent::Interpolate);

		registration::class_<BoxColliderComponent>("Snowstorm::BoxColliderComponent")
		    .constructor()
		    .property("HalfExtents", &BoxColliderComponent::HalfExtents)
		    .property("Offset", &BoxColliderComponent::Offset);

		registration::class_<SphereColliderComponent>("Snowstorm::SphereColliderComponent")
		    .constructor()
		    .property("Radius", &SphereColliderComponent::Radius)(metadata("Min", 0.001f))
		    .property("Offset", &SphereColliderComponent::Offset);

		registration::class_<CapsuleColliderComponent>("Snowstorm::CapsuleColliderComponent")
		    .constructor()
		    .property("Radius", &CapsuleColliderComponent::Radius)(metadata("Min", 0.001f))
		    .property("HalfHeight", &CapsuleColliderComponent::HalfHeight)(metadata("Min", 0.0f))
		    .property("Offset", &CapsuleColliderComponent::Offset);

		registration::class_<MeshColliderComponent>("Snowstorm::MeshColliderComponent")
		    .constructor()
		    .property("Mesh", &MeshColliderComponent::Mesh)(metadata("AssetType", static_cast<int>(AssetType::Mesh)))
		    .property("Convex", &MeshColliderComponent::Convex);
	}

	AUTO_REGISTER_COMPONENT(RigidBodyComponent);
	AUTO_REGISTER_COMPONENT(BoxColliderComponent);
	AUTO_REGISTER_COMPONENT(SphereColliderComponent);
	AUTO_REGISTER_COMPONENT(CapsuleColliderComponent);
	AUTO_REGISTER_COMPONENT(MeshColliderComponent);
}
