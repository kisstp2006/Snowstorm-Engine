#pragma once

#include <filesystem>

namespace Snowstorm::EnginePaths
{
	[[nodiscard]] std::filesystem::path FindRootFrom(std::filesystem::path startDirectory);
	[[nodiscard]] const std::filesystem::path& Root();
	[[nodiscard]] std::filesystem::path ShadersDirectory();
	[[nodiscard]] std::filesystem::path FontsDirectory();
	[[nodiscard]] std::filesystem::path CacheDirectory();

	// The .ssproj booted when nothing else selects one: startup.project is empty AND there is no recent
	// project to reopen (Runtime always, the editor only in an unattended run — see CVars::IsUnattended).
	// This is a REAL project on disk, not the implicit CWD-rooted project #105 retired: if it is missing the
	// caller fails loud instead of synthesizing one. Keeps the headless harnesses (smoke, perf-bench,
	// metrics) zero-config, the way Unreal falls back to the .uproject next to the binary.
	[[nodiscard]] std::filesystem::path DefaultProjectFile();

	// Where projects live by default: the folder DefaultProjectFile() sits in, and what the editor's
	// "New Project" dialog pre-fills as the location. One place decides this, so the picker's dialog and
	// the File-menu one can't drift. Unreal pre-fills a fixed projects folder the same way.
	[[nodiscard]] std::filesystem::path DefaultProjectsDirectory();
}
