#pragma once

#include "Window.hpp"
#include "Snowstorm/Core/LayerStack.hpp"
#include "Snowstorm/Events/Event.hpp"
#include "Snowstorm/Events/ApplicationEvent.hpp"
#include "Snowstorm/Events/EventBus.hpp"
#include "Snowstorm/Core/ModuleRegistry.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

namespace Snowstorm
{
	class Application : public NonCopyable
	{
	public:
		// `modules` is what this executable is made of (see Modules<...>()); initialized after the render
		// device exists and before any World. Core must be among them.
		explicit Application(const std::string& name, std::vector<Scope<IModule>> modules);
		~Application() override;

		// Run until a window/user request or an automated mode stops the application. Returning the stored
		// process status lets unattended startup failures propagate to scripts instead of looking successful.
		[[nodiscard]] int Run();

		void OnEvent(Event& e);

		void PushLayer(Layer* layer) const;

		Window& GetWindow() const { return *m_Window; }

		// Request a clean shutdown. A non-zero status is sticky so a later normal close cannot hide a failure.
		void Close(int exitCode = 0);

		static Application& Get() { return *s_Instance; }

		// True when an Application has been constructed. Lets engine objects (e.g. World) work in a
		// headless context (unit tests) where there is no Application/Window/Renderer.
		static bool Exists() { return s_Instance != nullptr; }

		ServiceManager& GetServiceManager() const { return *m_ServiceManager; }
		ModuleRegistry& GetModules() { return m_Modules; }
		const ModuleRegistry& GetModules() const { return m_Modules; }

		EventBus& GetEventBus() const { return *m_EventBus; }

	protected:
		Scope<ServiceManager> m_ServiceManager;
		ModuleRegistry m_Modules;

	private:
		Scope<Window> m_Window;
		Scope<EventBus> m_EventBus;
		bool m_Running = true;
		int m_ExitCode = 0;
		bool m_Minimized = false;
		Scope<LayerStack> m_LayerStack;
		float m_LastFrameTime = 0.0f;

		static Application* s_Instance;
	};

	// To be defined in CLIENT
	Application* CreateApplication();
}
