// SPDX-License-Identifier: GPL-3.0-or-later
// The adaptive triggers' payload and effect mapping (port/bettervr/xr/triggers.h), on a PC.
#include "bettervr/xr/triggers.h"

#include <iostream>

static int failures = 0;
static void Check(bool ok, const char* name)
{
	std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
	failures += !ok;
}

int main()
{
	using namespace ps5trig;
	Check(sizeof(Command) == 0x38, "a trigger command is 0x38 bytes");
	Check(sizeof(Param) == 0x78 && offsetof(Param, l2) == 0x08 && offsetof(Param, r2) == 0x40, "the parameter is 0x78 bytes with L2 at 0x08 and R2 at 0x40");

	const Param off = Both(Off(), Off());
	Check(off.mask == kMaskBoth && off.l2.mode == kOff && off.r2.mode == kOff, "off updates both triggers and leaves both commands off");
	Check(IsOff(off) && !IsOff(Both(Shield(1.0f), Off())), "only an off pair counts as off");

	const Command shield = Shield(1.0f);
	Check(shield.mode == kFeedback && shield.data[0] == 2 && shield.data[1] == 5, "the shield is feedback at position 2 with strength 5 at full strength");
	Check(Shield(0.5f).data[1] == 4 && Shield(0.0f).mode == kOff, "the shield follows the strength setting and is off at 0");

	Check(Bow(0.0f, 1.0f).mode == kOff && Bow(0.01f, 1.0f).mode == kOff, "a released bow is off");
	const Command half = Bow(0.5f, 1.0f);
	Check(half.mode == kSlope && half.data[0] == 0 && half.data[1] == 9 && half.data[2] == 1 && half.data[3] == 5,
		"half a draw ramps from 1 to 5 over the whole pull");
	Check(Bow(1.0f, 1.0f).data[3] == 8 && Bow(2.0f, 1.0f).data[3] == 8, "a full draw (and its 1.5 clamp) is strength 8");
	Check(Bow(1.0f, 0.0f).mode == kOff && Bow(1.0f, 0.5f).data[3] == 5, "the bow follows the strength setting and is off at 0");

	const Param both = Both(Shield(1.0f), Bow(1.0f, 1.0f));
	Check(both.mask == kMaskBoth && both.l2.mode == kFeedback && both.r2.mode == kSlope, "both effects share one 0x78 parameter");
	Check(Same(both, both) && !Same(both, Both(Shield(1.0f), Bow(0.0f, 1.0f))), "a change is seen by the sender's comparison");

	return failures ? 1 : 0;
}
