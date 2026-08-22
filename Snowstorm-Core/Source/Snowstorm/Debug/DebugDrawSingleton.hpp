#pragma once

#include "Snowstorm/ECS/Singleton.hpp"

#include <glm/vec3.hpp>

#include <cstdint>
#include <mutex>
#include <vector>

namespace Snowstorm
{
	// World-scoped immediate-mode debug lines (Unreal DrawDebugLine, Unity Debug.DrawLine): producers
	// (physics collider wireframes, any system) push world-space segments during the frame; the editor
	// viewport projects and draws them with its 2D overlay, then clears the list. No GPU pipeline — the
	// deliberate small version; a packaged runtime simply doesn't draw them. Thread-safe push (the physics
	// debug renderer may run off the main thread).
	class DebugDrawSingleton final : public Singleton
	{
	public:
		struct Line
		{
			glm::vec3 A;
			glm::vec3 B;
			uint32_t ColorABGR = 0xFF00FF00; // ImGui packed color (IM_COL32)
		};

		void Line3D(const glm::vec3& a, const glm::vec3& b, const uint32_t colorABGR)
		{
			std::lock_guard lock(m_Mutex);
			m_Lines.push_back({a, b, colorABGR});
		}

		[[nodiscard]] const std::vector<Line>& Lines() const { return m_Lines; }

		void Clear()
		{
			std::lock_guard lock(m_Mutex);
			m_Lines.clear();
		}

	private:
		std::mutex m_Mutex;
		std::vector<Line> m_Lines;
	};
}
