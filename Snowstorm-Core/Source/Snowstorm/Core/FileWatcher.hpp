#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Service/Service.hpp"

#include <filesystem>
#include <mutex>
#include <vector>

namespace Snowstorm
{
	struct FileEvent
	{
		enum class Kind : uint8_t
		{
			Created,
			Modified,
			Removed,
		};
		std::filesystem::path Path; // absolute
		Kind Type = Kind::Modified;
	};

	// One watched directory tree (Unreal FDirectoryWatcher, Unity's asset-folder watcher). The platform
	// implementation runs its own thread and appends raw OS notifications to a queue; the main thread
	// drains them once per frame (AssetWatchSystem), which also debounces the bursts editors produce.
	class FileWatcher
	{
	public:
		virtual ~FileWatcher() = default;
		[[nodiscard]] virtual const std::filesystem::path& Directory() const = 0;
		// Moves every queued event out (thread-safe).
		virtual void Drain(std::vector<FileEvent>& out) = 0;

		static Scope<FileWatcher> Create(const std::filesystem::path& directory);
	};

	// Application-scoped owner of the watchers: one per distinct directory (asset dir of the active
	// project, the engine shader dir). Watch() is idempotent; Drain() concatenates every watcher's queue.
	class FileWatcherService final : public Service
	{
	public:
		void Watch(const std::filesystem::path& directory);
		void Drain(std::vector<FileEvent>& out);

	private:
		std::vector<Scope<FileWatcher>> m_Watchers;
	};
}
