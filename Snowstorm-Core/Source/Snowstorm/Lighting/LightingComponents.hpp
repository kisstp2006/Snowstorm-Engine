#pragma once

#include "Snowstorm/Math/Math.hpp"

namespace Snowstorm
{
	// Light components (Hazel's Directional/Point/SpotLightComponent shape). `Radiance` is the light's
	// colour and `Intensity` its multiplier; the shadow knobs are per light instead of global CVars:
	//   - CastShadows  : this light writes a shadow at all
	//   - SoftShadows  : penumbra (PCF on the raster path, cone-jittered rays on the RT path). The
	//                    `render.shadow.soft` CVar is the master switch on top of this.
	//   - LightSize    : how big the emitter is, which is what makes a penumbra widen with distance.
	//   - ShadowAmount : how dark its shadow goes (1 = full). Multiplied by `render.shadow.strength`.

	struct DirectionalLightComponent
	{
		// Per-light on/off (Unity Light.enabled / Unreal SetVisibility / Godot Light3D.visible). A disabled
		// light is skipped in the gather, so it contributes no direct light and no shadow -- without deleting
		// the entity, so it can be toggled back on live. Distinct from CastShadows (which keeps the light but
		// drops its shadow) and from VisibilityComponent::Mask (which culls meshes, not lights).
		bool Enabled = true;

		glm::vec3 Direction{-0.3f, -1.0f, -0.2f};
		glm::vec3 Radiance{1.0f};
		float Intensity = 1.0f;

		bool CastShadows = true;
		bool SoftShadows = true;
		// Angular DIAMETER of the source disk in degrees — the sun is 0.53 deg. Bigger = softer penumbra
		// that widens with the distance from the blocker.
		float LightSize = 0.53f;
		float ShadowAmount = 1.0f;
	};

	struct PointLightComponent
	{
		bool Enabled = true; // per-light on/off; disabled lights are skipped in the gather (see DirectionalLightComponent::Enabled)

		glm::vec3 Radiance{1.0f};
		float Intensity = 1.0f;
		float Range = 10.0f; // distance at which the light's contribution smoothly reaches zero
		// Radius of the emitting sphere: inside it the inverse-square stops growing, so a light placed
		// close to a surface doesn't blow out to infinity.
		float MinRadius = 0.1f;
		// Shapes the range window: 1 = the default windowed inverse-square, towards 0 the falloff squares
		// up (light dies off closer to the source). Mirrors Hazel's Falloff blend.
		float Falloff = 1.0f;

		bool CastShadows = true;
		bool SoftShadows = true;
		float LightSize = 0.1f; // emitter radius in world units, drives the penumbra width
		float ShadowAmount = 1.0f;
	};

	struct SpotLightComponent
	{
		bool Enabled = true; // per-light on/off; disabled lights are skipped in the gather (see DirectionalLightComponent::Enabled)

		glm::vec3 Radiance{1.0f};
		float Intensity = 1.0f;
		float Range = 10.0f;
		float InnerAngleDeg = 20.0f; // full intensity within this half-angle
		float OuterAngleDeg = 30.0f; // zero past this half-angle (must be >= InnerAngleDeg)
		// Exponent on the inner->outer cone blend: 1 = linear edge, higher = the edge tightens towards the
		// outer angle (Hazel's AngleAttenuation).
		float AngleAttenuation = 2.0f;
		float Falloff = 1.0f; // same range-window shaping as PointLightComponent::Falloff

		bool CastShadows = true;
		bool SoftShadows = true;
		float LightSize = 0.1f; // emitter radius in world units
		float ShadowAmount = 1.0f;
	};
}
