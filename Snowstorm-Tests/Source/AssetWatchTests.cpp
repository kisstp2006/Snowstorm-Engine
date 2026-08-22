#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Systems/AssetWatchSystem.hpp"

using namespace Snowstorm;
using namespace std::chrono_literals;

TEST_CASE("AssetWatchSystem debounce settles a path only after it has been quiet", "[assets][watch]")
{
	AssetWatchSystem::PendingMap pending;
	const auto t0 = std::chrono::steady_clock::time_point{};

	// An editor save: create, several modifies, all within a few ms.
	AssetWatchSystem::Absorb(pending, {"C:/p/a.png", FileEvent::Kind::Created}, t0);
	AssetWatchSystem::Absorb(pending, {"C:/p/a.png", FileEvent::Kind::Modified}, t0 + 10ms);
	AssetWatchSystem::Absorb(pending, {"C:/p/a.png", FileEvent::Kind::Modified}, t0 + 30ms);
	AssetWatchSystem::Absorb(pending, {"C:/p/b.png", FileEvent::Kind::Modified}, t0 + 100ms);

	REQUIRE(AssetWatchSystem::Settle(pending, t0 + 200ms, 250ms).empty()); // nothing quiet for 250ms yet
	auto settled = AssetWatchSystem::Settle(pending, t0 + 300ms, 250ms);
	REQUIRE(settled.size() == 1); // a: last seen at 30ms -> quiet since; b: last seen at 100ms -> not yet
	REQUIRE(settled[0].first == std::filesystem::path("C:/p/a.png"));
	REQUIRE(settled[0].second == FileEvent::Kind::Modified); // last event wins
	settled = AssetWatchSystem::Settle(pending, t0 + 400ms, 250ms);
	REQUIRE(settled.size() == 1);
	REQUIRE(settled[0].first == std::filesystem::path("C:/p/b.png"));
	REQUIRE(pending.empty());
}

TEST_CASE("AssetWatchSystem debounce reports the last event kind", "[assets][watch]")
{
	AssetWatchSystem::PendingMap pending;
	const auto t0 = std::chrono::steady_clock::time_point{};
	AssetWatchSystem::Absorb(pending, {"C:/p/a.png", FileEvent::Kind::Removed}, t0);
	AssetWatchSystem::Absorb(pending, {"C:/p/a.png", FileEvent::Kind::Modified}, t0 + 5ms);
	const auto settled = AssetWatchSystem::Settle(pending, t0 + 1s, 250ms);
	REQUIRE(settled.size() == 1);
	REQUIRE(settled[0].second == FileEvent::Kind::Modified); // Dispatch re-checks existence either way
}
