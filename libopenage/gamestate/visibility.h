// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "gamestate/types.h"

namespace openage::gamestate {

enum class TileVisibility : uint8_t { Unexplored, Explored, Visible };

struct VisibleEntity {
	entity_id_t id;
	player_id_t owner;
	double x;
	double y;
};

struct VisibilitySnapshot {
	size_t width = 0;
	size_t height = 0;
	std::vector<TileVisibility> tiles;
	std::vector<VisibleEntity> entities;

	/** The snapshot stores currently visible entities in ID order. */
	bool contains(entity_id_t id) const {
		return std::ranges::binary_search(entities, id, {}, &VisibleEntity::id);
	}

	TileVisibility at(int64_t x, int64_t y) const {
		if (x < 0 or y < 0 or static_cast<size_t>(x) >= width or static_cast<size_t>(y) >= height) {
			return TileVisibility::Unexplored;
		}
		return tiles[static_cast<size_t>(y) * width + static_cast<size_t>(x)];
	}
};

} // namespace openage::gamestate
