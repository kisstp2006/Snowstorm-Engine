#include "ScriptRegistry.hpp"

#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Scripting/ScriptableEntity.hpp"

#include <algorithm>
#include <unordered_map>

namespace Snowstorm
{
	namespace
	{
		std::unordered_map<std::string, ScriptRegistry::Factory>& Factories()
		{
			static std::unordered_map<std::string, ScriptRegistry::Factory> s;
			return s;
		}
		std::vector<std::string>& SortedNames()
		{
			static std::vector<std::string> s;
			return s;
		}
	}

	void ScriptRegistry::Register(std::string name, Factory factory)
	{
		if (Factories().contains(name))
		{
			SS_CORE_WARN("ScriptRegistry: '{}' registered twice; keeping the first.", name);
			return;
		}
		SortedNames().push_back(name);
		std::ranges::sort(SortedNames());
		Factories().emplace(std::move(name), std::move(factory));
	}

	bool ScriptRegistry::Has(const std::string& name)
	{
		return Factories().contains(name);
	}

	Scope<ScriptableEntity> ScriptRegistry::Instantiate(const std::string& name)
	{
		const auto it = Factories().find(name);
		return it == Factories().end() ? nullptr : it->second();
	}

	const std::vector<std::string>& ScriptRegistry::Names()
	{
		return SortedNames();
	}
}
