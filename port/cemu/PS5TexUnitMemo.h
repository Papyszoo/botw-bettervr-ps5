// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the texture-unit memo a sequence's setup keeps (patches/cemu 0061,
// LatteTextureLegacy.cpp).
//
// A sequence's first draw resolves every texture unit its shaders use through Cemu's view lookup cache
// (LatteTextureViewLookupCache::lookup): the unit's SQ_TEX resource registers to a texture view, by
// scanning the bucket the registers' address, size and pitch hash to. The same registers at the same stage
// and unit resolve to the same view until a view is added to the cache or removed from it, so the answer
// is kept between sequences and the scan skipped. Everything the registers do not carry (the texture's
// data and swizzle, whether the unit is sampled as depth) is still read from the texture by the caller on
// every sequence, so a texture update or invalidation needs no notice here.
//
// The one thing that can change the answer behind the memo's back is the lookup cache itself: a view is
// added when one is made and removed when one goes. Both move the generation on, and an entry filled under
// an older one is not used, so a destroyed view is never handed back.
//
// Cemu's GPU thread is the only one that fills or reads an entry and the only one that adds or removes a
// view, so a plain load and store would do; the generation is an atomic so the header can be tested on a
// PC and stays sound if a view is ever let go on another thread.

#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

namespace ps5texunit
{
	// the view lookup cache's generation: every view added or removed moves it on
	inline std::atomic<uint32_t> viewGeneration{1};

	inline uint32_t Generation()
	{
		return viewGeneration.load(std::memory_order_relaxed);
	}

	// a view was added to the lookup cache or removed from it: the entries made before no longer hold
	inline void ViewsChanged()
	{
		viewGeneration.fetch_add(1, std::memory_order_relaxed);
	}

	// what the memo keys a unit by: the seven SQ_TEX resource words as the registers hold them, the stage
	// and unit they were read at, and whether the shader samples the unit as depth (a depth view is looked
	// up separately, so it is a different answer for the same words)
	struct Key
	{
		uint32_t words[7];
		uint32_t stage;
		uint32_t unit;
		bool isDepthSampler;

		bool operator==(const Key& other) const
		{
			return stage == other.stage && unit == other.unit && isDepthSampler == other.isDepthSampler &&
				std::memcmp(words, other.words, sizeof(words)) == 0;
		}
	};

	struct Entry
	{
		Key key{};
		uint32_t generation = 0;
		void* view = nullptr;
	};

	// whether the entry still answers the key: it holds a view, no view has been added or removed since it
	// was filled, and the words, stage, unit and depth comparison are the same
	inline bool Hit(const Entry& entry, const Key& key)
	{
		return entry.view != nullptr && entry.generation == Generation() && entry.key == key;
	}

	// the entry now answers the key with this view
	inline void Fill(Entry& entry, const Key& key, void* view)
	{
		entry.key = key;
		entry.generation = Generation();
		entry.view = view;
	}

	// nothing to answer with: the lookup found no view and none could be made
	inline void Clear(Entry& entry)
	{
		entry.view = nullptr;
	}
}
