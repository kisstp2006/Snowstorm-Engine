#include <Snowstorm.h>
#include <Snowstorm/Core/EntryPoint.hpp>

#include "EditorLayer.hpp"

#include "EditorModule.hpp"

#include <Snowstorm/Core/CoreModule.hpp>

namespace Snowstorm
{
	class SnowstormEditor final : public Application
	{
	public:
		SnowstormEditor()
		    : Application("Snowstorm-Editor", Modules<CoreModule, EditorModule>())
		{

			PushLayer(new EditorLayer());
		}
	};

	Application* CreateApplication()
	{
		return new SnowstormEditor();
	}
}
