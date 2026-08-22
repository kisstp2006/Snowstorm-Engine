#include "OrbitScript.hpp"

#include <Snowstorm/Components/TransformComponent.hpp>
#include <Snowstorm/Core/Log.hpp>

#include <glm/gtc/constants.hpp>

namespace Snowstorm
{
	void OrbitScript::OnStart()
	{
		if (!HasComponent<TransformComponent>())
		{
			return;
		}
		const glm::vec3 p = GetComponent<TransformComponent>().Position;
		m_Radius = std::max(0.5f, glm::length(glm::vec2(p.x, p.z)));
		m_Height = p.y;
		m_Angle = std::atan2(p.z, p.x);
	}

	void OrbitScript::OnUpdate(const Timestep ts)
	{
		if (!HasComponent<TransformComponent>())
		{
			return;
		}
		m_Angle += ts.GetSeconds() * glm::half_pi<float>(); // a quarter turn per second
		auto& tr = WriteComponent<TransformComponent>();
		tr.Position = {m_Radius * std::cos(m_Angle), m_Height, m_Radius * std::sin(m_Angle)};
	}

	void OrbitScript::OnFixedUpdate(const float /*fixedDt*/)
	{
		++m_FixedTicks; // proves the fixed-step phase drives scripts (visible in a debugger/test)
	}
}
