#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#include "System.hpp"
#include "SystemPhase.hpp"
#include "TrackedRegistry.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Debug/Instrumentor.hpp"
#include "Snowstorm/World/SimulationStateSingleton.hpp"

namespace Snowstorm
{
	class SystemManager final : public NonCopyable
	{
	public:
		using SystemTiming = std::pair<std::string, float>;

		explicit SystemManager(const System::WorldRef world)
		    : m_World(world)
		{
		}

		// `order` sorts systems within a phase (stable: equal orders keep registration order, so Core's
		// systems stay in their listed sequence and a module's default-0 systems append after them). A
		// negative order runs before Core's (e.g. the physics write-back before TransformSystem) — the
		// small version of Unity's OrderBefore/OrderAfter.
		template <typename T, typename... Args>
		void RegisterSystemOrdered(const SystemPhase phase, const int order, Args&&... args)
		{
			static_assert(std::is_base_of_v<System, T>, "T must inherit from System");
			const size_t p = static_cast<size_t>(phase);

			// Friendly name for the profiler: strip the leading "class Snowstorm::" MSVC prefix.
			std::string name = typeid(T).name();
			if (const size_t pos = name.rfind(':'); pos != std::string::npos)
			{
				name = name.substr(pos + 1);
			}

			// Insert before the first system with a greater order (stable).
			size_t at = m_Phases[p].size();
			for (size_t i = 0; i < m_Orders[p].size(); ++i)
			{
				if (m_Orders[p][i] > order)
				{
					at = i;
					break;
				}
			}
			const auto offset = static_cast<std::ptrdiff_t>(at);
			m_Phases[p].insert(m_Phases[p].begin() + offset, CreateScope<T>(m_World, std::forward<Args>(args)...));
			m_Orders[p].insert(m_Orders[p].begin() + offset, order);
			m_Timings[p].insert(m_Timings[p].begin() + offset, SystemTiming{std::move(name), 0.0f});
		}

		template <typename T, typename... Args>
		void RegisterSystem(const SystemPhase phase, Args&&... args)
		{
			RegisterSystemOrdered<T>(phase, 0, std::forward<Args>(args)...);
		}

		void ExecuteSystems(const Timestep ts)
		{
			using clock = std::chrono::steady_clock;

			// Edit mode gate: while stopped, skip simulation systems (those that opt out via
			// RunsInEditMode() == false). Resolved once per frame. A packaged runtime has no
			// SimulationStateSingleton, so `editMode` stays false and everything runs — the gate is
			// editor-only. (See SimulationStateSingleton / System::RunsInEditMode.)
			bool editMode = false;
			if (m_World && m_World->HasSingleton<SimulationStateSingleton>())
			{
				editMode = m_World->GetSingleton<SimulationStateSingleton>().Current == SimulationStateSingleton::Mode::Edit;
			}

			// Phases run in enum order; systems within a phase run in registration order. Time each
			// phase AND each system on the CPU so the editor overlay shows exactly where the frame goes.
			for (size_t i = 0; i < m_Phases.size(); ++i)
			{
				SS_PROFILE_SCOPE(SystemPhaseName(static_cast<SystemPhase>(i)));
				const auto phaseStart = clock::now();

				// FixedUpdate runs 0..kMaxFixedStepsPerFrame times at a fixed dt (Unity FixedUpdate / Godot
				// _physics_process): the accumulator carries the remainder across frames, and a stall is
				// clamped so the simulation never spirals. The phase's systems see the FIXED timestep.
				int iterations = 1;
				Timestep stepDt = ts;
				if (static_cast<SystemPhase>(i) == SystemPhase::FixedUpdate)
				{
					const float fixedDt = 1.0f / static_cast<float>(std::max(1, CVars::SimFixedHz.Get()));
					if (editMode || m_Phases[i].empty())
					{
						m_FixedAccumulator = 0.0f; // no simulation: don't bank time while paused in the editor
						iterations = 0;
					}
					else
					{
						m_FixedAccumulator = std::min(m_FixedAccumulator + ts.GetSeconds(), fixedDt * static_cast<float>(kMaxFixedStepsPerFrame));
						iterations = static_cast<int>(m_FixedAccumulator / fixedDt);
						m_FixedAccumulator -= static_cast<float>(iterations) * fixedDt;
						stepDt = Timestep{fixedDt};
					}
					m_FixedAlpha = m_FixedAccumulator / fixedDt;
					m_FixedDt = fixedDt;
				}

				for (size_t j = 0; j < m_Phases[i].size(); ++j)
				{
					m_Timings[i][j].second = 0.0f;
				}
				for (int step = 0; step < iterations; ++step)
				{
					for (size_t j = 0; j < m_Phases[i].size(); ++j)
					{
						System& sys = *m_Phases[i][j];
						if (editMode && !sys.RunsInEditMode())
						{
							continue; // skipped this frame (Edit mode)
						}
						SS_PROFILE_SCOPE(m_Timings[i][j].first.c_str());
						const auto sysStart = clock::now();
						sys.Execute(stepDt);
						const auto sysEnd = clock::now();
						m_Timings[i][j].second += std::chrono::duration<float, std::milli>(sysEnd - sysStart).count();
					}
				}
				const auto phaseEnd = clock::now();
				m_PhaseMs[i] = std::chrono::duration<float, std::milli>(phaseEnd - phaseStart).count();
			}

			m_Registry.ClearTrackedComponents();
		}

		TrackedRegistry& GetRegistry() { return m_Registry; }

		// Fixed-step state for interpolation: the fraction of a fixed step banked after this frame's
		// FixedUpdate (0..1) and the step length itself (a render-side system lerps prev->current by Alpha).
		[[nodiscard]] float FixedAlpha() const { return m_FixedAlpha; }
		[[nodiscard]] float FixedDeltaSeconds() const { return m_FixedDt; }

		// Per-phase CPU time (ms) for the most recent ExecuteSystems call, indexed by SystemPhase.
		[[nodiscard]] const std::array<float, static_cast<size_t>(SystemPhase::_Count)>& GetPhaseTimingsMs() const
		{
			return m_PhaseMs;
		}

		// Per-system (name, ms) for the most recent frame, grouped by phase (same order as execution).
		[[nodiscard]] const std::array<std::vector<SystemTiming>, static_cast<size_t>(SystemPhase::_Count)>& GetSystemTimingsMs() const
		{
			return m_Timings;
		}

	private:
		TrackedRegistry m_Registry;
		std::array<std::vector<Scope<System>>, static_cast<size_t>(SystemPhase::_Count)> m_Phases;
		std::array<std::vector<int>, static_cast<size_t>(SystemPhase::_Count)> m_Orders;
		std::array<float, static_cast<size_t>(SystemPhase::_Count)> m_PhaseMs{};
		std::array<std::vector<SystemTiming>, static_cast<size_t>(SystemPhase::_Count)> m_Timings;

		const System::WorldRef m_World;

		static constexpr int kMaxFixedStepsPerFrame = 4;
		float m_FixedAccumulator = 0.0f;
		float m_FixedAlpha = 0.0f;
		float m_FixedDt = 1.0f / 60.0f;
	};
}
