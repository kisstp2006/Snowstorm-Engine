#include "AssetWatchSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Assets/AssetMeta.hpp"
#include "Snowstorm/Core/EnginePaths.hpp"
#include "Snowstorm/Project/Project.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Shader.hpp"

namespace Snowstorm
{
	namespace
	{
		constexpr std::chrono::milliseconds kSettleMs{250};

		bool IsUnder(const std::filesystem::path& path, const std::filesystem::path& dir)
		{
			std::error_code ec;
			const auto rel = std::filesystem::relative(path, dir, ec);
			return !ec && !rel.empty() && rel.native()[0] != '.';
		}

		bool IsShaderSource(const std::filesystem::path& path)
		{
			const std::string ext = path.extension().string();
			return ext == ".hlsl" || ext == ".hlsli";
		}
	}

	void AssetWatchSystem::Absorb(PendingMap& pending, const FileEvent& ev, const std::chrono::steady_clock::time_point now)
	{
		Pending& p = pending[ev.Path.lexically_normal()];
		// Last event wins: the kind is only a hint — Dispatch re-checks whether the file exists when the
		// path settles, which is what decides between "re-import" and "unregister".
		p.Type = ev.Type;
		p.LastSeen = now;
	}

	std::vector<std::pair<std::filesystem::path, FileEvent::Kind>> AssetWatchSystem::Settle(PendingMap& pending, const std::chrono::steady_clock::time_point now, const std::chrono::milliseconds settle)
	{
		std::vector<std::pair<std::filesystem::path, FileEvent::Kind>> out;
		for (auto it = pending.begin(); it != pending.end();)
		{
			if (now - it->second.LastSeen >= settle)
			{
				out.emplace_back(it->first, it->second.Type);
				it = pending.erase(it);
			}
			else
			{
				++it;
			}
		}
		return out;
	}

	void AssetWatchSystem::Execute(Timestep /*ts*/)
	{
		if (!Application::Exists() || !Application::Get().GetServiceManager().ServiceRegistered<FileWatcherService>())
		{
			return;
		}
		auto& watcher = ServiceView<FileWatcherService>();

		// Arm the watchers lazily: the project is resolved after the World exists, and a project switch
		// creates a fresh World (so a new system instance arms the new asset dir).
		if (!m_WatchersArmed)
		{
			if (const Ref<Project> project = Project::GetActive())
			{
				watcher.Watch(project->GetAssetDirectory());
				watcher.Watch(EnginePaths::Root() / "Engine" / "Shaders");
				m_WatchersArmed = true;
			}
		}

		const auto now = std::chrono::steady_clock::now();
		m_Scratch.clear();
		watcher.Drain(m_Scratch);
		for (const FileEvent& ev : m_Scratch)
		{
			// Our own outputs are not sources: sidecars, temp files, the registry cache, cooked artifacts.
			const std::string ext = ev.Path.extension().string();
			if (ext == ".meta" || ext == ".tmp" || ext == ".json" || IsUnder(ev.Path, EnginePaths::Root() / "Engine" / "cache"))
			{
				continue;
			}
			Absorb(m_Pending, ev, now);
		}

		for (const auto& [path, kind] : Settle(m_Pending, now, kSettleMs))
		{
			Dispatch(path, kind);
		}
	}

	void AssetWatchSystem::Dispatch(const std::filesystem::path& path, const FileEvent::Kind kind)
	{
		std::error_code ec;
		const bool exists = std::filesystem::is_regular_file(path, ec);
		if (!exists && kind != FileEvent::Kind::Removed)
		{
			return; // transient (temp file already gone); a directory event
		}

		// Shaders: engine or project .hlsl/.hlsli. ReloadAll recompiles what changed (includes key the
		// cache), then every live pipeline whose shader version advanced rebuilds itself.
		if (IsShaderSource(path))
		{
			if (exists)
			{
				SS_CORE_INFO("Hot reload: shader '{}' changed.", path.filename().string());
				ServiceView<ShaderLibrary>().ReloadAll();
				Pipeline::ForEachLive([](const Ref<Pipeline>& pipeline)
				                      { pipeline->Reload(); });
			}
			return;
		}

		const Ref<Project> project = Project::GetActive();
		if (!project || !IsUnder(path, project->GetAssetDirectory()))
		{
			return;
		}
		const AssetType type = AssetTypeFromExtension(path.extension().string());
		if (type == AssetType::None)
		{
			return;
		}
		const std::filesystem::path rel = path.lexically_relative(project->GetProjectDirectory()).lexically_normal();
		SingletonView<AssetManagerSingleton>().OnSourceChanged(rel, type, !exists);
	}
}
