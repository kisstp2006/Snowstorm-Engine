#include "AnimationComponents.hpp"

#include "Snowstorm/Components/ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::class_<SkeletalMeshComponent>("Snowstorm::SkeletalMeshComponent")
		    .constructor()
		    .property("Skeleton", &SkeletalMeshComponent::Skeleton)(
		        metadata("AssetType", static_cast<int>(AssetType::Skeleton)));

		registration::class_<AnimationComponent>("Snowstorm::AnimationComponent")
		    .constructor()
		    .property("Clip", &AnimationComponent::Clip)(metadata("AssetType", static_cast<int>(AssetType::Animation)))
		    .property("Playing", &AnimationComponent::Playing)
		    .property("Loop", &AnimationComponent::Loop)
		    .property("Speed", &AnimationComponent::Speed)(metadata("Speed", 0.01f))
		    .property("Time", &AnimationComponent::Time)(metadata("Min", 0.0f), metadata("Speed", 0.01f));
	}

	AUTO_REGISTER_COMPONENT(SkeletalMeshComponent);
	AUTO_REGISTER_COMPONENT(AnimationComponent);

	namespace
	{
		// Derived every frame from the authored components, so it is neither saved with the scene nor
		// copied when an entity is duplicated (the copy re-resolves and re-binds on its first tick).
		struct AutoRegisterAnimationRuntime
		{
			AutoRegisterAnimationRuntime()
			{
				ComponentRegisterOptions opts{};
				opts.Serializable = false;
				opts.DrawInEditor = false;
				opts.Copyable = false;
				RegisterComponent<AnimationRuntimeComponent>(opts);
			}
		};
		const AutoRegisterAnimationRuntime g_autoRegisterAnimationRuntime;
	}
}
