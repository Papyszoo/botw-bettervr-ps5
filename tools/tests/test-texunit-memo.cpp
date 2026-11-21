// SPDX-License-Identifier: GPL-3.0-or-later
// The texture-unit memo's key and invalidation (port/cemu/PS5TexUnitMemo.h), on a PC.
//
// A sequence's texture unit is answered from the memo only while its SQ_TEX register words, stage, unit and
// depth comparison are bit-identical to the last resolution and no view has been added to or removed from
// the view lookup cache since. Anything else must miss: a missed hit is a wrong or freed view handed to the
// renderer.

#include "PS5TexUnitMemo.h"

#include <iostream>

static int failures = 0;
static void Check(bool ok, const char* name)
{
	std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
	failures += !ok;
}

int main()
{
	using namespace ps5texunit;

	int viewA = 1;
	int viewB = 2;

	Key key{};
	for (uint32_t i = 0; i < 7; i++)
		key.words[i] = 0xE0000000u + i * 0x00010101u;
	key.stage = 1; // pixel
	key.unit = 5;
	key.isDepthSampler = false;

	Entry entry;
	Check(!Hit(entry, key), "an empty entry misses");
	Fill(entry, key, &viewA);
	Check(Hit(entry, key), "a filled entry hits the same key");
	Check(entry.view == &viewA && entry.generation == Generation(), "a hit holds the view and the generation it was filled under");

	Key other = key;
	other.words[3] ^= 1;
	Check(!Hit(entry, other), "a different word in any of the seven is a miss");
	other = key;
	other.words[6] ^= 0x00000001u;
	Check(!Hit(entry, other), "the words are compared bit for bit, not as values");
	other = key;
	other.stage = 0;
	Check(!Hit(entry, other), "the same words at another stage are a miss");
	other = key;
	other.unit = 6;
	Check(!Hit(entry, other), "the same words at another unit are a miss");
	other = key;
	other.isDepthSampler = true;
	Check(!Hit(entry, other), "the same words sampled as depth are a miss (a depth view is looked up separately)");
	Check(Hit(entry, key), "a miss does not disturb the entry");

	// a view destroyed and another made at the same address: the words still match, the view must not
	ViewsChanged();
	Check(!Hit(entry, key), "a view added or removed lets go of every entry");
	Fill(entry, key, &viewB);
	Check(Hit(entry, key) && entry.view == &viewB, "the entry filled after the change answers with the new view");
	Fill(entry, key, &viewA);
	Check(Hit(entry, key) && entry.view == &viewA, "filled again, it answers with the view it was given last");

	Clear(entry);
	Check(!Hit(entry, key), "a cleared entry misses");

	const uint32_t before = Generation();
	ViewsChanged();
	Check(Generation() == before + 1, "each change moves the generation on");

	return failures ? 1 : 0;
}
