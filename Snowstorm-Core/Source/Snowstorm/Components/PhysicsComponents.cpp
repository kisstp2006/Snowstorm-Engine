#include "PhysicsComponents.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::enumeration<EBodyType>("Snowstorm::EBodyType")(
		    value("Static", EBodyType::Static),
		    value("Dynamic", EBodyType::Dynamic),
		    value("Kinematic", EBodyType::Kinematic));
		registration::enumeration<ECollisionDetectionType>("Snowstorm::ECollisionDetectionType")(
		    value("Discrete", ECollisionDetectionType::Discrete),
		    value("Continuous", ECollisionDetectionType::Continuous));
		registration::enumeration<ECollisionComplexity>("Snowstorm::ECollisionComplexity")(
		    value("Default", ECollisionComplexity::Default),
		    value("UseComplexAsSimple", ECollisionComplexity::UseComplexAsSimple),
		    value("UseSimpleAsComplex", ECollisionComplexity::UseSimpleAsComplex));

		registration::class_<ColliderMaterial>("Snowstorm::ColliderMaterial")
		    .constructor()(policy::ctor::as_object)
		    .property("Friction", &ColliderMaterial::Friction)(metadata("Min", 0.0f))
		    .property("Restitution", &ColliderMaterial::Restitution)(metadata("Min", 0.0f), metadata("Max", 1.0f));

		registration::class_<RigidBodyComponent>("Snowstorm::RigidBodyComponent")
		    .constructor()
		    .property("BodyType", &RigidBodyComponent::BodyType)
		    .property("LayerID", &RigidBodyComponent::LayerID)(metadata("Min", 0), metadata("Max", 31))
		    .property("EnableDynamicTypeChange", &RigidBodyComponent::EnableDynamicTypeChange)
		    .property("Mass", &RigidBodyComponent::Mass)(metadata("Min", 0.001f))
		    .property("LinearDrag", &RigidBodyComponent::LinearDrag)(metadata("Min", 0.0f))
		    .property("AngularDrag", &RigidBodyComponent::AngularDrag)(metadata("Min", 0.0f))
		    .property("DisableGravity", &RigidBodyComponent::DisableGravity)
		    .property("IsTrigger", &RigidBodyComponent::IsTrigger)
		    .property("CollisionDetection", &RigidBodyComponent::CollisionDetection)
		    .property("InitialLinearVelocity", &RigidBodyComponent::InitialLinearVelocity)
		    .property("InitialAngularVelocity", &RigidBodyComponent::InitialAngularVelocity)
		    .property("MaxLinearVelocity", &RigidBodyComponent::MaxLinearVelocity)(metadata("Min", 0.0f))
		    .property("MaxAngularVelocity", &RigidBodyComponent::MaxAngularVelocity)(metadata("Min", 0.0f))
		    .property("LockedAxes", &RigidBodyComponent::LockedAxes)(
		        metadata("Flags", FlagBitList{{"TranslationX", 1u << 0}, {"TranslationY", 1u << 1}, {"TranslationZ", 1u << 2},
		                                      {"RotationX", 1u << 3}, {"RotationY", 1u << 4}, {"RotationZ", 1u << 5}}));

		registration::class_<CompoundColliderComponent>("Snowstorm::CompoundColliderComponent")
		    .constructor()
		    .property("IncludeStaticChildColliders", &CompoundColliderComponent::IncludeStaticChildColliders)
		    .property("IsImmutable", &CompoundColliderComponent::IsImmutable)
		    .property("CompoundedColliderEntities", &CompoundColliderComponent::CompoundedColliderEntities)(metadata("Hidden", true));

		registration::class_<BoxColliderComponent>("Snowstorm::BoxColliderComponent")
		    .constructor()
		    .property("HalfSize", &BoxColliderComponent::HalfSize)
		    .property("Offset", &BoxColliderComponent::Offset)
		    .property("Material", &BoxColliderComponent::Material);

		registration::class_<SphereColliderComponent>("Snowstorm::SphereColliderComponent")
		    .constructor()
		    .property("Radius", &SphereColliderComponent::Radius)(metadata("Min", 0.001f))
		    .property("Offset", &SphereColliderComponent::Offset)
		    .property("Material", &SphereColliderComponent::Material);

		registration::class_<CapsuleColliderComponent>("Snowstorm::CapsuleColliderComponent")
		    .constructor()
		    .property("Radius", &CapsuleColliderComponent::Radius)(metadata("Min", 0.001f))
		    .property("HalfHeight", &CapsuleColliderComponent::HalfHeight)(metadata("Min", 0.0f))
		    .property("Offset", &CapsuleColliderComponent::Offset)
		    .property("Material", &CapsuleColliderComponent::Material);

		registration::class_<MeshColliderComponent>("Snowstorm::MeshColliderComponent")
		    .constructor()
		    .property("ColliderAsset", &MeshColliderComponent::ColliderAsset)(metadata("AssetType", static_cast<int>(AssetType::Mesh)))
		    .property("SubmeshIndex", &MeshColliderComponent::SubmeshIndex)(metadata("Min", 0))
		    .property("UseSharedShape", &MeshColliderComponent::UseSharedShape)
		    .property("Material", &MeshColliderComponent::Material)
		    .property("CollisionComplexity", &MeshColliderComponent::CollisionComplexity);
	}

	AUTO_REGISTER_COMPONENT(RigidBodyComponent);
	AUTO_REGISTER_COMPONENT(CompoundColliderComponent);
	AUTO_REGISTER_COMPONENT(BoxColliderComponent);
	AUTO_REGISTER_COMPONENT(SphereColliderComponent);
	AUTO_REGISTER_COMPONENT(CapsuleColliderComponent);
	AUTO_REGISTER_COMPONENT(MeshColliderComponent);
}
