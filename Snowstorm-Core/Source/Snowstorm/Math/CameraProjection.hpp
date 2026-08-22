#pragma once

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/mat4x4.hpp>

namespace Snowstorm
{
	// Pure camera math shared by CameraRuntimeUpdateSystem (the per-frame cache) and the editor overlay,
	// which must project with the CURRENT frame's camera pose before that cache is rebuilt (the UI phase
	// precedes Resolve; using the cache there lags the scene by one frame when the camera moves).

	inline float ComputeCameraAspect(const CameraComponent& cam, const ViewportComponent& vp)
	{
		if (cam.FixedAspectRatio && cam.AspectRatio > 0.0001f)
			return cam.AspectRatio;
		return (vp.Size.y > 0.0001f) ? (vp.Size.x / vp.Size.y) : (16.0f / 9.0f);
	}

	// Vulkan clip space (0..1 Z, glm RH_ZO); the Y flip happens in the renderer, not here.
	inline glm::mat4 BuildCameraProjection(const CameraComponent& cam, const float aspect)
	{
		if (cam.Projection == CameraComponent::ProjectionType::Perspective)
		{
			return glm::perspectiveRH_ZO(cam.PerspectiveFOV, aspect, cam.PerspectiveNear, cam.PerspectiveFar);
		}
		const float halfH = cam.OrthographicSize * 0.5f; // OrthographicSize = height in world units
		const float halfW = halfH * aspect;
		return glm::ortho(-halfW, halfW, -halfH, halfH, cam.OrthographicNear, cam.OrthographicFar);
	}
}
