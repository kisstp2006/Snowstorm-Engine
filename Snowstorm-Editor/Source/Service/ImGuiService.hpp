#pragma once

#include "Snowstorm/Core/Timestep.hpp"
#include "Snowstorm/Service/Service.hpp"

namespace Snowstorm
{
	class ImGuiService final : public Service
	{
	public:
		ImGuiService();
		~ImGuiService() override;

		void OnUpdate(Timestep ts) override;
		void PostUpdate(Timestep ts) override;

	private:
		// Whether OnUpdate actually opened an ImGui frame this tick (it skips a minimized window, where
		// ImGui asserts on a zero DisplaySize). PostUpdate closes only a frame that was opened.
		bool m_FrameBegun = false;
	};
}
