#pragma once

#include "Snowstorm/Core/FileWatcher.hpp"
#include "Snowstorm/ECS/System.hpp"

#include <chrono>
#include <filesystem>
#include <map>

namespace Snowstorm
{
	// Drains the FileWatcherService once per frame (AssetSync phase) and turns settled file changes into
	// hot reloads: project sources go to AssetManagerSingleton::OnSourceChanged (re-import + swap the
	// live GPU object), engine/project shaders go to ShaderLibrary::ReloadAll + pipeline rebuild. Editors
	// write a file in several bursts (truncate, write, rename), so an event only fires after the path has
	// been quiet for kSettleMs (Unity/Unreal both debounce the same way).
	class AssetWatchSystem final : public System
	{
	public:
		explicit AssetWatchSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

		// Pure debounce core (unit-tested): feed raw events with a timestamp, collect the paths that have
		// been quiet for `settle`; the last event's kind is reported (Dispatch re-checks the filesystem).
		struct Pending
		{
			FileEvent::Kind Type = FileEvent::Kind::Modified;
			std::chrono::steady_clock::time_point LastSeen;
		};
		using PendingMap = std::map<std::filesystem::path, Pending>;
		static void Absorb(PendingMap& pending, const FileEvent& ev, std::chrono::steady_clock::time_point now);
		static std::vector<std::pair<std::filesystem::path, FileEvent::Kind>> Settle(PendingMap& pending, std::chrono::steady_clock::time_point now, std::chrono::milliseconds settle);

	private:
		void Dispatch(const std::filesystem::path& path, FileEvent::Kind kind);

		PendingMap m_Pending;
		std::vector<FileEvent> m_Scratch;
		bool m_WatchersArmed = false;
	};
}
