#include "ModuleRegistry.hpp"

#include "Snowstorm/Core/Log.hpp"

#include <algorithm>
#include <functional>
#include <string_view>
#include <unordered_map>

namespace Snowstorm
{
	void ModuleRegistry::Add(Scope<IModule> module)
	{
		SS_CORE_VERIFY(!m_Initialized, "ModuleRegistry::Add after Initialize is not supported");
		m_Modules.push_back(std::move(module));
	}

	std::vector<size_t> ModuleRegistry::Resolve(const std::vector<IModule*>& modules, std::string& error)
	{
		std::unordered_map<std::string_view, size_t> byName;
		for (size_t i = 0; i < modules.size(); ++i)
		{
			if (!byName.emplace(modules[i]->Name(), i).second)
			{
				error = std::string("duplicate module '") + modules[i]->Name() + "'";
				return {};
			}
		}

		// Depth-first post-order; visiting in declaration order keeps the result stable for independent
		// modules (Core stays first when it is listed first).
		std::vector<size_t> order;
		std::vector<uint8_t> state(modules.size(), 0); // 0 = new, 1 = visiting, 2 = done
		std::function<bool(size_t)> visit = [&](const size_t i) -> bool
		{
			if (state[i] == 2)
			{
				return true;
			}
			if (state[i] == 1)
			{
				error = std::string("dependency cycle through module '") + modules[i]->Name() + "'";
				return false;
			}
			state[i] = 1;
			for (const char* dep : modules[i]->Dependencies())
			{
				const auto it = byName.find(dep);
				if (it == byName.end())
				{
					error = std::string("module '") + modules[i]->Name() + "' depends on unknown module '" + dep + "'";
					return false;
				}
				if (!visit(it->second))
				{
					return false;
				}
			}
			state[i] = 2;
			order.push_back(i);
			return true;
		};
		for (size_t i = 0; i < modules.size(); ++i)
		{
			if (!visit(i))
			{
				return {};
			}
		}
		return order;
	}

	void ModuleRegistry::Initialize(ServiceManager& services)
	{
		SS_CORE_VERIFY(!m_Initialized, "ModuleRegistry::Initialize called twice");
		m_Initialized = true;

		std::vector<IModule*> raw;
		raw.reserve(m_Modules.size());
		for (const auto& m : m_Modules)
		{
			raw.push_back(m.get());
		}
		std::string error;
		const std::vector<size_t> order = Resolve(raw, error);
		SS_CORE_VERIFY(!error.empty() == order.empty(), "ModuleRegistry: inconsistent resolve");
		if (order.empty() && !m_Modules.empty())
		{
			SS_CORE_ERROR("ModuleRegistry: {} — no module will be initialized.", error);
			m_Modules.clear();
			return;
		}

		std::vector<Scope<IModule>> sorted;
		sorted.reserve(order.size());
		for (const size_t i : order)
		{
			sorted.push_back(std::move(m_Modules[i]));
		}
		m_Modules = std::move(sorted);

		for (const auto& m : m_Modules)
		{
			m->RegisterTypes();
		}
		for (const auto& m : m_Modules)
		{
			m->RegisterServices(services);
			SS_CORE_INFO("Module '{}' initialized.", m->Name());
		}
	}

	void ModuleRegistry::RegisterWorld(World& world) const
	{
		for (const auto& m : m_Modules)
		{
			m->RegisterWorld(world);
		}
	}

	void ModuleRegistry::Shutdown()
	{
		for (auto it = m_Modules.rbegin(); it != m_Modules.rend(); ++it)
		{
			(*it)->Shutdown();
		}
		m_Modules.clear();
	}

	bool ModuleRegistry::IsLoaded(const std::string& name) const
	{
		return std::ranges::any_of(m_Modules, [&](const auto& m)
		                           { return name == m->Name(); });
	}

	std::vector<std::string> ModuleRegistry::OrderedNames() const
	{
		std::vector<std::string> names;
		for (const auto& m : m_Modules)
		{
			names.emplace_back(m->Name());
		}
		return names;
	}
}
