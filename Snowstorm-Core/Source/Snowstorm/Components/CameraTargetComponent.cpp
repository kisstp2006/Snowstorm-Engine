#include "CameraTargetComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::class_<CameraTargetComponent>("Snowstorm::CameraTargetComponent")
		    .property("TargetViewportUUID", &CameraTargetComponent::TargetViewportUUID)(
		        metadata("EntityRef", true) // a UUID naming another entity, not an asset
		    );
	}

	AUTO_REGISTER_COMPONENT(CameraTargetComponent);
}
