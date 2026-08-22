#include "Snowstorm/Core/FileWatcher.hpp"
#include "Snowstorm/Core/Log.hpp"

#ifdef SS_PLATFORM_WINDOWS

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <thread>

namespace Snowstorm
{
	namespace
	{
		// ReadDirectoryChangesW on a dedicated thread with overlapped I/O; a manual-reset event wakes the
		// wait for shutdown. Notifications are pushed raw (no coalescing) — the consumer debounces.
		class WindowsFileWatcher final : public FileWatcher
		{
		public:
			explicit WindowsFileWatcher(std::filesystem::path directory)
			    : m_Directory(std::move(directory))
			{
				m_Handle = CreateFileW(m_Directory.c_str(), FILE_LIST_DIRECTORY,
				                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
				                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
				if (m_Handle == INVALID_HANDLE_VALUE)
				{
					SS_CORE_WARN("FileWatcher: cannot open '{}' (error {}).", m_Directory.string(), GetLastError());
					return;
				}
				m_StopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				m_Thread = std::thread([this]
				                       { Run(); });
			}

			~WindowsFileWatcher() override
			{
				m_Stopping = true;
				if (m_StopEvent)
				{
					SetEvent(m_StopEvent);
				}
				if (m_Handle != INVALID_HANDLE_VALUE)
				{
					CancelIoEx(m_Handle, nullptr);
				}
				if (m_Thread.joinable())
				{
					m_Thread.join();
				}
				if (m_Handle != INVALID_HANDLE_VALUE)
				{
					CloseHandle(m_Handle);
				}
				if (m_StopEvent)
				{
					CloseHandle(m_StopEvent);
				}
			}

			[[nodiscard]] const std::filesystem::path& Directory() const override { return m_Directory; }

			void Drain(std::vector<FileEvent>& out) override
			{
				std::lock_guard lock(m_Mutex);
				out.insert(out.end(), std::make_move_iterator(m_Queue.begin()), std::make_move_iterator(m_Queue.end()));
				m_Queue.clear();
			}

		private:
			void Run()
			{
				alignas(DWORD) char buffer[64 * 1024];
				OVERLAPPED overlapped{};
				overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				const HANDLE waits[2] = {overlapped.hEvent, m_StopEvent};
				constexpr DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
				                         FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;

				while (!m_Stopping)
				{
					ResetEvent(overlapped.hEvent);
					if (!ReadDirectoryChangesW(m_Handle, buffer, sizeof(buffer), TRUE, filter, nullptr, &overlapped, nullptr))
					{
						break;
					}
					const DWORD which = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
					DWORD bytes = 0;
					if (which != WAIT_OBJECT_0 || m_Stopping)
					{
						// Shutting down with the read still pending: the kernel owns `buffer`/`overlapped` until
						// the I/O completes, so cancel it and WAIT for the completion before this frame unwinds.
						// Returning early here let a late completion scribble over a dead stack (random crash
						// at exit).
						CancelIoEx(m_Handle, &overlapped);
						GetOverlappedResult(m_Handle, &overlapped, &bytes, TRUE);
						break;
					}
					if (!GetOverlappedResult(m_Handle, &overlapped, &bytes, FALSE) || bytes == 0)
					{
						continue; // buffer overflow (bytes == 0): the consumer rescans on its own cadence anyway
					}

					std::vector<FileEvent> batch;
					const char* cursor = buffer;
					for (;;)
					{
						const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(cursor);
						const std::wstring name(info->FileName, info->FileNameLength / sizeof(WCHAR));
						FileEvent ev;
						ev.Path = m_Directory / name;
						switch (info->Action)
						{
						case FILE_ACTION_ADDED:
						case FILE_ACTION_RENAMED_NEW_NAME:
							ev.Type = FileEvent::Kind::Created;
							break;
						case FILE_ACTION_REMOVED:
						case FILE_ACTION_RENAMED_OLD_NAME:
							ev.Type = FileEvent::Kind::Removed;
							break;
						default:
							ev.Type = FileEvent::Kind::Modified;
							break;
						}
						batch.push_back(std::move(ev));
						if (info->NextEntryOffset == 0)
						{
							break;
						}
						cursor += info->NextEntryOffset;
					}
					std::lock_guard lock(m_Mutex);
					m_Queue.insert(m_Queue.end(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));
				}
				CloseHandle(overlapped.hEvent);
			}

			std::filesystem::path m_Directory;
			HANDLE m_Handle = INVALID_HANDLE_VALUE;
			HANDLE m_StopEvent = nullptr;
			std::thread m_Thread;
			std::atomic<bool> m_Stopping{false};
			std::mutex m_Mutex;
			std::vector<FileEvent> m_Queue;
		};
	}

	Scope<FileWatcher> FileWatcher::Create(const std::filesystem::path& directory)
	{
		return CreateScope<WindowsFileWatcher>(directory);
	}
}

#endif
