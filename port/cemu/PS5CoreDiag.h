// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: what each emulated core was doing when a diagnostic looked at it (patches/cemu 0065).
//
// A long frame on Cemu's GPU thread, or a hole in the AX output, is either the emulated CPU busy
// with the game's own code, or one of its cores sitting in a host call (a forced readback, a driver
// submit, a file read) while the frame waits. Neither is in any log today. So each emulated core
// publishes a few atomics about itself as it changes state, and the port's log lines (the GPU
// thread's [stall] line, the audio output thread's AX gap line) sample them: only reads, no locks,
// and no per-call bookkeeping on any path the guest runs through.
//
// Published on the emulated core itself:
// - __OSLoadThread: which thread runs there (name, priority) -> CoreRunning
// - the idle fiber's loop -> CoreIdle
// - the HLE dispatch (the recompiler's and the interpreter's virtualHLE) -> HostEnter / HostLeave,
//   with the export's name from the HLE table
// - the two blocking paths that matter at a warp: TCL's GPU timestamp wait -> WaitEnter(GpuSync)
// - the AX thread: AXIst_QueueFrame and AXIst_ThreadEntry -> AxQueued / AxTaken / AxDone, so a
//   frame queued but not started says the AX thread was runnable and not scheduled
//
// Read by the [stall] line (GPU thread) and PS5AudioAPI's gap line (audio output thread), both host
// threads that already exist. Cost per event: a handful of relaxed stores; cost per sample: a few
// dozen loads, two or three times a second at most.
//
// A host call that runs a guest callback (GX2's allocator callbacks) is reported as executing from
// the callback's return until the call returns: the outermost call is what a long block is made of,
// and the callback itself really is guest code.

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace ps5diag
{
	constexpr int kCoreCount = 3;
	constexpr int kNameBytes = 40; // 5 eight-byte words, kept in atomics (see CoreSlot)

	enum class State : uint32_t
	{
		Unknown = 0, // the core has not run anything yet
		Executing,	 // running the guest's PowerPC code
		Host,		 // inside a host call (the HLE export's name and how long are kept)
		Idle		 // the idle fiber: no emulated thread to run
	};

	// What a host call is blocked on, when a path says; otherwise the export's name says it
	enum class Wait : uint32_t
	{
		None = 0,
		GpuSync,  // a GPU/GX2 timestamp or the command ring
		Coreinit, // a coreinit primitive (event, thread queue, semaphore, message)
		FileIO
	};

	struct CoreSample
	{
		State state = State::Unknown;
		char threadName[kNameBytes] = {};
		int32_t priority = -1;
		const char* hostCall = nullptr; // the HLE export, a static string, "" when not in a call
		uint64_t insideNs = 0;			// how long the core has been in it
		Wait wait = Wait::None;			// what the call is blocked on, when known
		uint64_t waitNs = 0;
	};

	struct AxSample
	{
		bool queued = false;	  // a frame message is queued and the AX thread has not taken it
		uint64_t queuedNs = 0;	  // how long it has been waiting to be taken
		bool processing = false;  // taken, mixing
		uint64_t processingNs = 0;
		uint64_t sinceFrameNs = 0; // since the AX thread finished the last frame
	};

	struct Sample
	{
		CoreSample core[kCoreCount];
		AxSample ax;
	};

	namespace detail
	{
		struct CoreSlot
		{
			std::atomic<uint32_t> state{0};			  // State, released after the fields below
			std::atomic<uint32_t> nameSeq{0};		  // odd while the name and priority are written
			std::atomic<uint64_t> nameWord[kNameBytes / 8]{}; // the thread's name, packed
			std::atomic<int32_t> priority{-1};
			std::atomic<const char*> hostCall{nullptr}; // a static HLE name
			std::atomic<uint64_t> hostCallSince{0};
			std::atomic<uint32_t> wait{0}; // Wait
			std::atomic<uint64_t> waitSince{0};
		};

		inline CoreSlot g_core[kCoreCount];
		inline std::atomic<uint64_t> g_axQueuedAt{0}, g_axTakenAt{0}, g_axDoneAt{0};

		inline bool InRange(int core) { return core >= 0 && core < kCoreCount; }
	}

	// the core's current emulated thread changed: it is about to run the guest's code
	inline void CoreRunning(int core, const char* name, int32_t priority)
	{
		using namespace detail;
		if (!InRange(core))
			return;
		CoreSlot& slot = g_core[core];
		char text[kNameBytes] = {};
		if (name)
		{
			// a guest string: stop at the line's room, and at the first byte that is not printable,
			// so a name cannot carry control bytes into the log or split the line
			for (int i = 0; i < kNameBytes - 1 && name[i]; i++)
			{
				const unsigned char c = (unsigned char)name[i];
				if (c < 0x20 || c > 0x7E)
					break;
				text[i] = (char)c;
			}
		}
		uint64_t word[kNameBytes / 8] = {};
		std::memcpy(word, text, sizeof(text));
		// the seqlock: odd while written, so a sampler between the two bumps reads the old name again
		slot.nameSeq.fetch_add(1, std::memory_order_release);
		for (int i = 0; i < kNameBytes / 8; i++)
			slot.nameWord[i].store(word[i], std::memory_order_relaxed);
		slot.priority.store(priority, std::memory_order_relaxed);
		slot.nameSeq.fetch_add(1, std::memory_order_release);
		slot.hostCall.store(nullptr, std::memory_order_relaxed);
		slot.wait.store((uint32_t)Wait::None, std::memory_order_relaxed);
		slot.state.store((uint32_t)State::Executing, std::memory_order_release);
	}

	// the core entered its idle fiber: no emulated thread is runnable there. The wait fields are left
	// alone: a core is idle here because its last thread went to sleep, and what it waits on is what
	// the line should say (CoreRunning clears them when another thread is loaded)
	inline void CoreIdle(int core)
	{
		using namespace detail;
		if (!InRange(core))
			return;
		CoreSlot& slot = g_core[core];
		if (slot.state.load(std::memory_order_relaxed) == (uint32_t)State::Idle)
			return; // every turn of the idle loop asks: write it once
		slot.hostCall.store(nullptr, std::memory_order_relaxed);
		slot.state.store((uint32_t)State::Idle, std::memory_order_release);
	}

	// the core is inside a host call: name is a static HLE export name, nowNs Cemu's clock
	inline void HostEnter(int core, const char* exportName, uint64_t nowNs)
	{
		using namespace detail;
		if (!InRange(core))
			return;
		CoreSlot& slot = g_core[core];
		slot.hostCallSince.store(nowNs, std::memory_order_relaxed);
		slot.hostCall.store(exportName, std::memory_order_relaxed);
		slot.wait.store((uint32_t)Wait::None, std::memory_order_relaxed);
		slot.state.store((uint32_t)State::Host, std::memory_order_release);
	}

	inline void HostLeave(int core)
	{
		using namespace detail;
		if (!InRange(core))
			return;
		CoreSlot& slot = g_core[core];
		slot.hostCall.store(nullptr, std::memory_order_relaxed);
		slot.wait.store((uint32_t)Wait::None, std::memory_order_relaxed);
		slot.state.store((uint32_t)State::Executing, std::memory_order_release);
	}

	// the host call is blocked on something the name alone does not say (TCL's GPU timestamp)
	inline void WaitEnter(int core, Wait wait, uint64_t nowNs)
	{
		using namespace detail;
		if (!InRange(core))
			return;
		CoreSlot& slot = g_core[core];
		slot.waitSince.store(nowNs, std::memory_order_relaxed);
		slot.wait.store((uint32_t)wait, std::memory_order_release);
	}

	inline void WaitLeave(int core)
	{
		using namespace detail;
		if (!InRange(core))
			return;
		g_core[core].wait.store((uint32_t)Wait::None, std::memory_order_release);
	}

	// the AX thread: a frame message was queued, taken by the AX thread, and finished
	inline void AxQueued(uint64_t nowNs)
	{
		detail::g_axQueuedAt.store(nowNs, std::memory_order_release);
	}

	inline void AxTaken(uint64_t nowNs)
	{
		detail::g_axTakenAt.store(nowNs, std::memory_order_release);
	}

	inline void AxDone(uint64_t nowNs)
	{
		detail::g_axDoneAt.store(nowNs, std::memory_order_release);
	}

	inline Sample Take(uint64_t nowNs)
	{
		using namespace detail;
		Sample sample;
		for (int core = 0; core < kCoreCount; core++)
		{
			CoreSlot& slot = g_core[core];
			CoreSample& out = sample.core[core];
			out.priority = slot.priority.load(std::memory_order_relaxed);
			// the name, seqlock-read: two equal even sequences are one whole write
			for (int attempt = 0; attempt < 3; attempt++)
			{
				const uint32_t before = slot.nameSeq.load(std::memory_order_acquire);
				if (before & 1)
					continue;
				uint64_t word[kNameBytes / 8];
				for (int i = 0; i < kNameBytes / 8; i++)
					word[i] = slot.nameWord[i].load(std::memory_order_relaxed);
				out.priority = slot.priority.load(std::memory_order_relaxed);
				if (slot.nameSeq.load(std::memory_order_acquire) != before)
					continue;
				std::memcpy(out.threadName, word, sizeof(word));
				break;
			}
			out.state = (State)slot.state.load(std::memory_order_acquire);
			// the wait is read whatever the state: a core that is idle is idle because a thread went
			// to sleep, and the reason is the useful part; the waiting thread clears it when it wakes
			out.wait = (Wait)slot.wait.load(std::memory_order_acquire);
			const uint64_t waitSince = slot.waitSince.load(std::memory_order_relaxed);
			out.waitNs = out.wait != Wait::None && waitSince && nowNs > waitSince ? nowNs - waitSince : 0;
			if (out.state == State::Host)
			{
				out.hostCall = slot.hostCall.load(std::memory_order_relaxed);
				const uint64_t since = slot.hostCallSince.load(std::memory_order_relaxed);
				out.insideNs = since && nowNs > since ? nowNs - since : 0;
			}
		}
		const uint64_t queuedAt = g_axQueuedAt.load(std::memory_order_acquire);
		const uint64_t takenAt = g_axTakenAt.load(std::memory_order_acquire);
		const uint64_t doneAt = g_axDoneAt.load(std::memory_order_acquire);
		sample.ax.queued = queuedAt > takenAt;
		sample.ax.queuedNs = sample.ax.queued && nowNs > queuedAt ? nowNs - queuedAt : 0;
		sample.ax.processing = takenAt > doneAt;
		sample.ax.processingNs = sample.ax.processing && nowNs > takenAt ? nowNs - takenAt : 0;
		sample.ax.sinceFrameNs = doneAt && nowNs > doneAt ? nowNs - doneAt : 0;
		return sample;
	}

	// The name alone classifies a waiting export: coreinit's OS* primitives, its FS* calls, GX2/TCL
	// and the file services. No work at publication time; only a sample pays for it.
	inline Wait WaitOf(const char* exportName)
	{
		if (!exportName)
			return Wait::None;
		auto starts = [exportName](const char* prefix) { return std::strncmp(exportName, prefix, std::strlen(prefix)) == 0; };
		if (starts("gx2.") || starts("GX2") || starts("TCL.") || starts("tcl."))
			return Wait::GpuSync;
		if (starts("coreinit.FS") || starts("coreinit.IOS") || starts("iosu") || starts("nn.fs") || starts("IOS"))
			return Wait::FileIO;
		if (starts("coreinit.OS"))
			return Wait::Coreinit;
		return Wait::None;
	}

	inline const char* WaitText(Wait wait)
	{
		switch (wait)
		{
		case Wait::GpuSync:
			return "a GPU sync";
		case Wait::Coreinit:
			return "a coreinit primitive";
		case Wait::FileIO:
			return "file I/O";
		default:
			return "";
		}
	}

	inline std::string Ms(uint64_t ns)
	{
		char text[32];
		std::snprintf(text, sizeof(text), "%.1f ms", (double)ns / 1e6);
		return text;
	}

	// The line both callers print: one compact sentence per core, then the AX thread. Measured
	// values only: everything here is what the sampler read at that instant, or the age of a
	// timestamp the core wrote about itself.
	inline std::string Describe(const Sample& sample)
	{
		std::string text = "cores:";
		for (int core = 0; core < kCoreCount; core++)
		{
			const CoreSample& it = sample.core[core];
			text += " " + std::to_string(core) + " ";
			switch (it.state)
			{
			case State::Idle:
				text += "idle";
				if (it.wait != Wait::None)
					text += std::string(" (a thread has waited ") + Ms(it.waitNs) + " on " + WaitText(it.wait) + ")";
				break;
			case State::Executing:
				text += "executing";
				break;
			case State::Host:
			{
				text += it.hostCall ? std::string("in ") + it.hostCall : std::string("in a host call");
				text += " for " + Ms(it.insideNs);
				// a path that knows what it blocks on says so (TCL's GPU timestamp); otherwise the
				// export's name classifies it: a coreinit primitive, file I/O, a GPU sync
				const Wait wait = it.wait != Wait::None ? it.wait : WaitOf(it.hostCall);
				if (wait != Wait::None)
					text += std::string(", waiting on ") + WaitText(wait);
				break;
			}
			default:
				text += "not started";
				break;
			}
			if (it.state == State::Executing || it.state == State::Host)
			{
				text += it.threadName[0] ? std::string(", '") + it.threadName + "'" : std::string(", no thread name");
				if (it.priority >= 0)
					text += " prio " + std::to_string(it.priority);
			}
			text += ";";
		}
		text += " AX thread:";
		if (sample.ax.queued)
			text += " a frame is queued " + Ms(sample.ax.queuedNs) + " and not taken (runnable on core 1, not scheduled)";
		else if (sample.ax.processing)
			text += " mixing this frame for " + Ms(sample.ax.processingNs);
		else if (sample.ax.sinceFrameNs)
			text += " waiting for the next frame (the last finished " + Ms(sample.ax.sinceFrameNs) + " ago)";
		else
			text += " no frame yet";
		return text;
	}
}
