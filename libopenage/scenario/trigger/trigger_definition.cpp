// Copyright 2026 the openage authors. See copying.md for legal info.

#include "scenario/trigger/trigger_definition.h"

#include <cstdint>


namespace openage::scenario::trigger {


TriggerId make_trigger_id(const std::string &trigger_id) {
	// FNV-1a 64-bit, offset basis 0xcbf29ce484222325.
	// Stable across platforms and inputs; same string always
	// maps to the same id.
	constexpr uint64_t FNV_OFFSET = 0xcbf29ce484222325ULL;
	constexpr uint64_t FNV_PRIME = 0x100000001b3ULL;
	uint64_t h = FNV_OFFSET;
	for (unsigned char c : trigger_id) {
		h ^= static_cast<uint64_t>(c);
		h *= FNV_PRIME;
	}
	// Reserve 0 as NULL_TRIGGER_ID. 0 maps to "" (empty string)
	// so a degenerate trigger_id still produces a valid id.
	if (h == NULL_TRIGGER_ID) {
		h = 1;
	}
	return h;
}


}  // namespace openage::scenario::trigger
