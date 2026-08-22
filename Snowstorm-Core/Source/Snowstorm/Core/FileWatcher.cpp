#include "FileWatcher.hpp"

#include <algorithm>

namespace Snowstorm
{
	void FileWatcherService::Watch(const std::filesystem::path& directory)
	{
		std::error_code ec;
		const std::filesystem::path canon = std::filesystem::weakly_canonical(directory, ec);
		if (ec || !std::filesystem::is_directory(canon, ec))
		{
			return;
		}
		const bool already = std::ranges::any_of(m_Watchers, [&](const auto& w)
		                                         { return w->Directory() == canon; });
		if (already)
		{
			return;
		}
		if (auto watcher = FileWatcher::Create(canon))
		{
			m_Watchers.push_back(std::move(watcher));
		}
	}

	void FileWatcherService::Drain(std::vector<FileEvent>& out)
	{
		for (const auto& w : m_Watchers)
		{
			w->Drain(out);
		}
	}
}
