// SPDX-License-Identifier: GPL-3.0-or-later
// BotW-BetterVR on PS5, stage 2 of the BetterVR 1.0.0 port (docs/bettervr-ps5/UPDATE-1.0.md): what
// BetterVR's host module (.deps/BotW-BetterVR/src, patches/bettervr) takes from the app in place of
// platform_linux.cpp, built into the module's library by module.cmake:
//
//   Platform::      the functions src/utils/platform.h asks of the platform
//   the log         BetterVR's lines in the app's boot log
//   __atomic_*      the generic atomic functions libc++ lowers a std::atomic of a large struct to

#include "utils/platform.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

// the app's: the boot log's line, and the app's folder (VrPS5.cpp)
void PS5Cemu_LogLine(std::string_view line);
extern "C" const char* BetterVR_PS5_AppDir();

// BetterVR's lines, which logger.cpp writes to its file, in the boot log as well: every line a
// [bettervr] line (the file has the same, with the time)
void BetterVR_PS5_LogBatch(const char* data, size_t size)
{
	std::string_view rest(data, size);
	while (!rest.empty())
	{
		const size_t end = rest.find('\n');
		const std::string_view line = rest.substr(0, end);
		if (!line.empty())
			PS5Cemu_LogLine(std::string("[bettervr] ").append(line));
		if (end == std::string_view::npos)
			break;
		rest.remove_prefix(end + 1);
	}
}

void Platform::FailFast()
{
	std::abort();
}

// Where the module looks for BetterVR/packs (host_module.cpp's FindPacksDirectory): tools/package.sh puts
// them in the app's assets/bettervr, as the Windows download has them next to Cemu
std::filesystem::path Platform::GetExecutableDirectory()
{
	return std::filesystem::path(BetterVR_PS5_AppDir()) / "assets" / "bettervr";
}

// there is no browser: a link in BetterVR's menus (the update, its pages) is only said
bool Platform::OpenUrl(const char*)
{
	return false;
}

// nothing else shares the block: the Linux build's named shared memory, as plain memory
Platform::SharedMemoryBlock::SharedMemoryBlock(const char*, size_t size)
{
	m_view = std::calloc(1, size);
	if (m_view != nullptr)
		m_size = size;
}

void Platform::SharedMemoryBlock::Reset()
{
	std::free(m_view);
	m_view = nullptr;
	m_size = 0;
}

// The generic atomic functions (libatomic's, which clang calls for a std::atomic of a struct wider than
// 16 bytes): the system's libSceLibcInternal has them, but its __atomic_load died with SIGILL on the
// 0x6c0 bytes of the 0.9.23 mod's input state (the clean-room libc.prx does not export them either),
// so the app has its own, a lock for the address each. The names are the compiler's own, so they are
// given as assembler labels. The sizes the compiler inlines never come here.
namespace
{
	std::mutex& LockFor(const volatile void* address)
	{
		static std::mutex locks[64];
		return locks[(reinterpret_cast<uintptr_t>(address) >> 4) % 64];
	}
}

extern "C"
{
	void bettervr_atomic_load(size_t size, void* from, void* to, int) __asm__("__atomic_load");
	void bettervr_atomic_store(size_t size, void* to, void* from, int) __asm__("__atomic_store");
	void bettervr_atomic_exchange(size_t size, void* object, void* value, void* old, int) __asm__("__atomic_exchange");
	bool bettervr_atomic_compare_exchange(size_t size, void* object, void* expected, void* desired, int, int)
		__asm__("__atomic_compare_exchange");

	void bettervr_atomic_load(size_t size, void* from, void* to, int)
	{
		std::lock_guard lock(LockFor(from));
		std::memcpy(to, from, size);
	}

	void bettervr_atomic_store(size_t size, void* to, void* from, int)
	{
		std::lock_guard lock(LockFor(to));
		std::memcpy(to, from, size);
	}

	void bettervr_atomic_exchange(size_t size, void* object, void* value, void* old, int)
	{
		std::lock_guard lock(LockFor(object));
		std::memcpy(old, object, size);
		std::memcpy(object, value, size);
	}

	bool bettervr_atomic_compare_exchange(size_t size, void* object, void* expected, void* desired, int, int)
	{
		std::lock_guard lock(LockFor(object));
		if (std::memcmp(object, expected, size) == 0)
		{
			std::memcpy(object, desired, size);
			return true;
		}
		std::memcpy(expected, object, size);
		return false;
	}
}
