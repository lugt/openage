// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4B: Footprint -- the gameplay geometry of an entity.
//
// PLAIN DATA CLASS (not an openage Component). The Component base
// is non-copyable and has no default constructor (BaseCurve's
// copy is explicitly deleted), which is a poor fit for pure
// geometry data the validator, the occupancy grid, and the
// viewmodel all need to copy/return. Footprint is stored on the
// entity as a field (entity->set_footprint) and read by the
// validator, the StaticOccupancyGrid, and the viewmodel layer.
//
// Geometry:
//   - axis-aligned rectangle (P4.4B scope).
//   - center anchor (the entity's placement position is the
//     rectangle's center).
//   - bounds = [center - radius, center + radius) in tile units
//     (1 tile = 1 cell = 65536 phys = 1 world unit).
//   - half-open [min, max): a tile whose center is inside the
//     bounds is covered.
//   - no sprite pixels: this is gameplay/navigation footprint,
//     not render bounds.
//
// Source hierarchy (per spec §IV):
//   nyan Collision.hitbox (radius_x, radius_y in world units = tiles)
//   -> Footprint(radius_x, radius_y, blocks_*, is_static)
//   -> entity->set_footprint(...)
//   -> PlacementValidator + StaticOccupancyGrid.

#pragma once

#include <cstdint>

#include "gamestate/types.h"


namespace openage::gamestate::component {


enum class FootprintShape : uint8_t {
	AXIS_ALIGNED_RECTANGLE = 0,
};


struct FootprintBounds {
	double min_x{0.0};
	double min_y{0.0};
	double max_x{0.0};
	double max_y{0.0};

	bool overlaps(const FootprintBounds &o) const {
		return min_x < o.max_x && o.min_x < max_x
		    && min_y < o.max_y && o.min_y < max_y;
	}
	bool contains(double x, double y) const {
		return x >= min_x && x < max_x
		    && y >= min_y && y < max_y;
	}
};


class Footprint {
public:
	Footprint() = default;
	Footprint(const Footprint &) = default;
	Footprint &operator=(const Footprint &) = default;
	Footprint(Footprint &&) = default;
	Footprint &operator=(Footprint &&) = default;

	static Footprint make_rectangle(
		double radius_x_tiles,
		double radius_y_tiles,
		bool blocks_placement,
		bool blocks_pathfinding,
		bool is_static);

	FootprintShape shape() const { return this->shape_; }
	double radius_x() const { return this->radius_x_; }
	double radius_y() const { return this->radius_y_; }
	bool blocks_placement() const { return this->blocks_placement_; }
	bool blocks_pathfinding() const { return this->blocks_pathfinding_; }
	bool is_static() const { return this->is_static_; }

	void set_center(double center_x, double center_y) {
		this->bounds_.min_x = center_x - this->radius_x_;
		this->bounds_.min_y = center_y - this->radius_y_;
		this->bounds_.max_x = center_x + this->radius_x_;
		this->bounds_.max_y = center_y + this->radius_y_;
	}

	void set_extents(double rx, double ry) {
		this->radius_x_ = rx > 0.0 ? rx : 0.0;
		this->radius_y_ = ry > 0.0 ? ry : 0.0;
	}
	void set_blocks_placement(bool v) { this->blocks_placement_ = v; }
	void set_blocks_pathfinding(bool v) { this->blocks_pathfinding_ = v; }
	void set_is_static(bool v) { this->is_static_ = v; }

	bool overlaps(const FootprintBounds &other) const {
		return this->bounds_.overlaps(other);
	}
	FootprintBounds bounds() const { return this->bounds_; }

private:
	FootprintShape shape_{FootprintShape::AXIS_ALIGNED_RECTANGLE};
	double radius_x_{0.0};
	double radius_y_{0.0};
	bool blocks_placement_{false};
	bool blocks_pathfinding_{false};
	bool is_static_{false};
	FootprintBounds bounds_{};
};


} // namespace openage::gamestate::component
