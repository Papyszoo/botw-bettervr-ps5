// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: what a core publishes about itself for the long-frame line, on a PC
// (port/cemu/PS5CoreDiag.h).
//
// The [stall] line's second line is only as good as the state behind it: a core must say it is
// executing, inside a host call (which export, since when), idle, or waiting on something, and it
// must say it without the reader taking a lock. This runs the publication and the sampling the
// console does: the states and their transitions, the thread name and priority, the wait kinds and
// the names that classify them, a name longer than the line holds, and the AX thread's three
// timestamps. Every sample is taken with an explicit clock, as the sampler does.

#include "PS5CoreDiag.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
	int failures = 0;

	void Check(bool ok, const char* what)
	{
		if (ok)
			return;
		std::printf("  FAIL: %s\n", what);
		failures++;
	}

	bool Has(const std::string& text, const char* part)
	{
		return text.find(part) != std::string::npos;
	}
}

int main()
{
	using namespace ps5diag;
	constexpr uint64_t kMs = 1000000;

	// a thread just loaded on core 1: executing, with its name and priority
	CoreRunning(1, "WorkThread", 16);
	{
		const Sample s = Take(1000 * kMs);
		Check(s.core[1].state == State::Executing, "core 1 executing after a thread load");
		Check(std::strcmp(s.core[1].threadName, "WorkThread") == 0, "the thread's name");
		Check(s.core[1].priority == 16, "the thread's priority");
		Check(s.core[1].hostCall == nullptr, "no host call while executing");
		Check(Has(Describe(s), "1 executing, 'WorkThread' prio 16"), "the line says who runs");
	}

	// the same thread 8 ms into GX2DrawDone, whose TCL wait says it is blocked on the GPU
	HostEnter(1, "gx2.GX2DrawDone", 1000 * kMs);
	WaitEnter(1, Wait::GpuSync, 1008 * kMs);
	{
		const Sample s = Take(1042 * kMs);
		Check(s.core[1].state == State::Host, "core 1 inside a host call");
		Check(std::strcmp(s.core[1].hostCall, "gx2.GX2DrawDone") == 0, "the export's name");
		Check(s.core[1].insideNs == 42 * kMs, "how long it has been inside");
		Check(s.core[1].wait == Wait::GpuSync, "the TCL wait kind");
		Check(s.core[1].waitNs == 34 * kMs, "how long it has waited");
		const std::string line = Describe(s);
		Check(Has(line, "1 in gx2.GX2DrawDone for 42.0 ms, waiting on a GPU sync, 'WorkThread' prio 16"), line.c_str());
		Check(Has(line, "0 not started"), "a core that never ran is named");
	}

	// the call returns: executing again, no wait left behind
	WaitLeave(1);
	HostLeave(1);
	{
		const Sample s = Take(1050 * kMs);
		Check(s.core[1].state == State::Executing, "executing after the host call");
		Check(s.core[1].hostCall == nullptr, "the host call is cleared");
		Check(s.core[1].wait == Wait::None, "the wait is cleared");
	}

	// a thread goes to sleep on a GPU timestamp: the core is idle while it waits, and the reason is
	// kept across the switch, because that is what the line has to say about the idle core
	CoreRunning(1, "WorkThread", 16);
	HostEnter(1, "gx2.GX2DrawDone", 1055 * kMs);
	WaitEnter(1, Wait::GpuSync, 1060 * kMs);
	CoreIdle(1);
	{
		const Sample s = Take(1070 * kMs);
		Check(s.core[1].state == State::Idle, "core 1 idle");
		Check(s.core[1].wait == Wait::GpuSync, "idle keeps what the thread waits on");
		Check(Has(Describe(s), "1 idle (a thread has waited 10.0 ms on a GPU sync)"), Describe(s).c_str());
	}
	// the thread is loaded again: the wait is cleared by the load, not carried into the next thread
	CoreRunning(1, "WorkThread", 16);
	{
		const Sample s = Take(1075 * kMs);
		Check(s.core[1].wait == Wait::None, "loading a thread clears the wait");
	}

	// the name is cut to the line's room, not read past it
	CoreRunning(0, "A thread name that is much too long for the line to hold", 10);
	{
		const Sample s = Take(1080 * kMs);
		Check(std::strlen(s.core[0].threadName) == kNameBytes - 1, "a long name is truncated");
		Check(std::strcmp(s.core[0].threadName, "A thread name that is much too long for") == 0, "the kept part");
	}

	// the export's name classifies a wait the paths do not mark: coreinit's primitives, its files
	// and the GPU, with no work on the publication path
	Check(WaitOf("coreinit.OSWaitEvent") == Wait::Coreinit, "coreinit.OS* is a coreinit primitive");
	Check(WaitOf("coreinit.FSReadFileWithPos") == Wait::FileIO, "coreinit.FS* is file I/O");
	Check(WaitOf("gx2.GX2WaitTimeStamp") == Wait::GpuSync, "gx2.* is a GPU sync");
	Check(WaitOf("TCL.TCLWaitTimestamp") == Wait::GpuSync, "TCL.* is a GPU sync");
	Check(WaitOf("vpad.VPADRead") == Wait::None, "a plain host call has no wait kind");
	Check(WaitOf(nullptr) == Wait::None, "no export, no wait");

	// the AX thread: queued and not taken is runnable but not scheduled; taken is mixing; done is
	// waiting for the next frame
	AxQueued(2000 * kMs);
	{
		const Sample s = Take(2012 * kMs);
		Check(s.ax.queued && !s.ax.processing, "a queued frame is not taken");
		Check(s.ax.queuedNs == 12 * kMs, "how long it has waited to be taken");
		Check(Has(Describe(s), "a frame is queued 12.0 ms and not taken (runnable on core 1, not scheduled)"),
			Describe(s).c_str());
	}
	AxTaken(2013 * kMs);
	{
		const Sample s = Take(2016 * kMs);
		Check(!s.ax.queued && s.ax.processing, "a taken frame is mixing");
		Check(s.ax.processingNs == 3 * kMs, "how long it has been mixing");
		Check(Has(Describe(s), "mixing this frame for 3.0 ms"), Describe(s).c_str());
	}
	AxDone(2019 * kMs);
	{
		const Sample s = Take(2024 * kMs);
		Check(!s.ax.queued && !s.ax.processing, "a finished frame is neither queued nor mixing");
		Check(s.ax.sinceFrameNs == 5 * kMs, "since the last finished frame");
		Check(Has(Describe(s), "waiting for the next frame (the last finished 5.0 ms ago)"), Describe(s).c_str());
	}

	if (failures)
		return 1;
	std::printf("core diagnostics: the states, the export's name, the wait kinds and the AX thread's timestamps\n");
	return 0;
}
