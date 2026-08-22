#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Snowstorm
{
	struct RecentProject
	{
		std::string Name;
		std::filesystem::path Path;
		int64_t LastOpened = 0;
	};

	class EditorPreferences
	{
	public:
		static bool Load();
		static bool Save();
		static void RecordProject(const std::string& name, const std::filesystem::path& path);

		// Drop one entry from the recent list and persist. The project manager offers this so an entry whose
		// project moved or was deleted can be cleared without editing the preferences file by hand.
		static void RemoveProject(const std::filesystem::path& path);
		static void RemoveMissingProjects();

		[[nodiscard]] static const std::vector<RecentProject>& RecentProjects() { return s_RecentProjects; }
		[[nodiscard]] static std::filesystem::path FilePath();

	private:
		inline static std::vector<RecentProject> s_RecentProjects;
	};
}
