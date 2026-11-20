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
	Check(offsetof(Command, mode) == 0x00 && offsetof(Command, data) == 0x08, "a command's mode is at 0x00 and its data at 0x08");
	Check(sizeof(Param) == 0x78 && offsetof(Param, mask) == 0x00 && offsetof(Param, l2) == 0x08 && offsetof(Param, r2) == 0x40,
		"the parameter is 0x78 bytes with the mask at 0x00, L2 at 0x08 and R2 at 0x40");

	const Param off = Both(Off(), Off());
	Check(off.mask == kMaskBoth && off.l2.mode == kOff && off.r2.mode == kOff, "off updates both triggers and leaves both commands off");
	Check(IsOff(off) && !IsOff(Both(Shield(1.0f), Off())), "only an off pair counts as off");

	const Command shield = Shield(1.0f);
	Check(shield.mode == kFeedback && shield.data[0] == 2 && shield.data[1] == 5, "the shield is feedback at position 2 with strength 5 at full strength");
	Check(Shield(0.5f).data[1] == 4 && Shield(0.0f).mode == kOff, "the shield follows the strength setting and is off at 0");

	Check(Bow(0.0f, 1.0f).mode == kOff && Bow(0.01f, 1.0f).mode == kOff, "a released bow is off");
	const Command half = Bow(0.5f, 1.0f);
	Check(half.mode == kFeedback && half.data[0] == 2 && half.data[1] == 5,
		"half a draw is feedback at position 2 with strength 5");
	Check(Bow(1.0f, 1.0f).mode == kFeedback && Bow(1.0f, 1.0f).data[1] == 8 && Bow(2.0f, 1.0f).data[1] == 8,
		"a full draw (and its 1.5 clamp) is feedback at strength 8");
	Check(Bow(1.0f, 0.0f).mode == kOff && Bow(1.0f, 0.5f).data[1] == 5, "the bow follows the strength setting and is off at 0");
	bool drawsInRange = true;
	for (int i = 0; i <= 10; i++)
	{
		const Command draw = Bow(i / 10.0f, 1.0f);
		drawsInRange = drawsInRange && draw.mode != kSlope && (draw.mode == kOff || (draw.data[1] >= 1 && draw.data[1] <= 8));
	}
	Check(drawsInRange, "every draw stays in feedback mode with strength 1-8 and never asks for slope mode 5");

	const Param both = Both(Shield(1.0f), Bow(1.0f, 1.0f));
	Check(both.mask == kMaskBoth && both.l2.mode == kFeedback && both.r2.mode == kFeedback, "both effects share one 0x78 parameter");
	Check(Same(both, both) && !Same(both, Both(Shield(1.0f), Bow(0.0f, 1.0f))), "a change is seen by the sender's comparison");

	Check(kTestPayloadCount == 11, "the Vibration page's trigger test has eleven steps");
	Check(kTestPayloads[0].mode == kFeedback && kTestPayloads[0].data[0] == 2 && kTestPayloads[0].data[1] == 5,
		"step 1 is the shield's accepted feedback 2 5");
	Check(kTestPayloads[2].mode == kFeedback && kTestPayloads[2].data[0] == 2 && kTestPayloads[2].data[1] == 8,
		"step 3 is the bow's full-draw feedback 2 8");
	Check(kTestPayloads[3].mode == kWeapon && kTestPayloads[3].data[0] == 4 && kTestPayloads[3].data[1] == 6 && kTestPayloads[3].data[2] == 5,
		"step 4 is the weapon mode 2 (4, 6, 5)");
	Check(kTestPayloads[4].mode == kVibration && kTestPayloads[4].data[0] == 2 && kTestPayloads[4].data[1] == 8 && kTestPayloads[4].data[2] == 40,
		"step 5 is vibration 2, 8, 40 Hz");
	Check(kTestPayloads[5].mode == kMultiFeedback && kTestPayloads[5].data[9] == 8, "step 6 is multi-position feedback");
	Check(kTestPayloads[6].mode == kSlope && kTestPayloads[6].data[0] == 0 && kTestPayloads[6].data[1] == 9, "step 7 is our old slope payload");
	Check(kTestPayloads[7].mode == kSlope && kTestPayloads[7].data[0] == 1 && kTestPayloads[7].data[1] == 9, "step 8 is AstroBotVR2's slope payload");
	Check(kTestPayloads[8].mode == kMultiVibration && kTestPayloads[8].data[0] == 40, "step 9 is multi-position vibration, frequency first");
	Check(kTestPayloads[9].mode == kOff, "step 10 is off");
	Check(kTestPayloads[10].mode == kOff && kTestPayloads[10].data[0] == 0, "step 11 is all off");

	const Param testParam = TestParam(kTestPayloads[2]);
	Check(testParam.mask == kMaskBoth && testParam.l2.mode == kFeedback && testParam.r2.mode == kFeedback,
		"a test step puts the same command on both triggers");
	Check(testParam.l2.data[0] == 2 && testParam.l2.data[1] == 8 && testParam.r2.data[0] == 2 && testParam.r2.data[1] == 8,
		"a test step's data bytes are copied into both commands");

	return failures ? 1 : 0;
}
