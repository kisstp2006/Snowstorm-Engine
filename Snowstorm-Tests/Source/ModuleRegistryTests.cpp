#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Core/ModuleRegistry.hpp"

#include <array>
#include <string>
#include <vector>

using namespace Snowstorm;

namespace
{
	struct FakeModule final : IModule
	{
		std::string N;
		std::vector<const char*> Deps;
		FakeModule(std::string n, std::vector<const char*> d = {})
		    : N(std::move(n)), Deps(std::move(d))
		{
		}
		[[nodiscard]] const char* Name() const override { return N.c_str(); }
		[[nodiscard]] std::span<const char* const> Dependencies() const override { return Deps; }
	};
}

TEST_CASE("ModuleRegistry::Resolve orders dependencies first and keeps declaration order otherwise", "[modules]")
{
	FakeModule editor{"Editor", {"Core"}};
	FakeModule core{"Core"};
	FakeModule physics{"Physics", {"Core"}};
	FakeModule game{"Game", {"Physics", "Editor"}};
	std::vector<IModule*> mods{&editor, &core, &physics, &game};

	std::string error;
	const std::vector<size_t> order = ModuleRegistry::Resolve(mods, error);
	REQUIRE(error.empty());
	REQUIRE(order.size() == 4);
	std::vector<std::string> names;
	for (const size_t i : order)
		names.emplace_back(mods[i]->Name());
	REQUIRE(names == std::vector<std::string>{"Core", "Editor", "Physics", "Game"});
}

TEST_CASE("ModuleRegistry::Resolve reports cycles and unknown dependencies", "[modules]")
{
	std::string error;
	{
		FakeModule a{"A", {"B"}};
		FakeModule b{"B", {"A"}};
		std::vector<IModule*> mods{&a, &b};
		REQUIRE(ModuleRegistry::Resolve(mods, error).empty());
		REQUIRE(error.find("cycle") != std::string::npos);
	}
	{
		FakeModule a{"A", {"Missing"}};
		std::vector<IModule*> mods{&a};
		REQUIRE(ModuleRegistry::Resolve(mods, error).empty());
		REQUIRE(error.find("unknown module 'Missing'") != std::string::npos);
	}
	{
		FakeModule a{"A"};
		FakeModule a2{"A"};
		std::vector<IModule*> mods{&a, &a2};
		REQUIRE(ModuleRegistry::Resolve(mods, error).empty());
		REQUIRE(error.find("duplicate") != std::string::npos);
	}
}
