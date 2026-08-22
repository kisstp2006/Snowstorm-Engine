#pragma once

#include "Snowstorm/Core/Base.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Snowstorm
{
	class ScriptableEntity;

	// Name -> factory for native script classes (what makes a ScriptComponent's ClassName resolvable and
	// serializable; Unity finds MonoBehaviours by type name the same way). Modules register their scripts
	// from RegisterTypes via SS_REGISTER_SCRIPT.
	class ScriptRegistry
	{
	public:
		using Factory = std::function<Scope<ScriptableEntity>()>;

		static void Register(std::string name, Factory factory);
		[[nodiscard]] static bool Has(const std::string& name);
		// Null when unknown (ScriptSystem warns once per name).
		static Scope<ScriptableEntity> Instantiate(const std::string& name);
		[[nodiscard]] static const std::vector<std::string>& Names(); // sorted, for the inspector combo
	};
}

// Registers `Type` under its unqualified class name.
#define SS_REGISTER_SCRIPT(Type) \
	::Snowstorm::ScriptRegistry::Register(#Type, []() -> ::Snowstorm::Scope<::Snowstorm::ScriptableEntity> { return ::Snowstorm::CreateScope<Type>(); })
