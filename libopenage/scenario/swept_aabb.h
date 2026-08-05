// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4F.1.5: swept_aabb -- conservative swept AABB for a
// single-axis movement segment. Used by the Move runtime to
// detect dynamic-occupancy collisions along the entire
// segment, not just at the endpoints.
//
// Why this matters:
//   The Move runtime currently checks the destination cell
//   against DynamicOccupancy. That's not enough: a unit
//   could start in cell A, end in cell B, but pass through
//   cell C on the way (e.g. a diagonal step), and the
//   check-at-end rule would miss the collision. The swept
//   AABB expands the destination's bounds to cover the
//   entire segment, so the collision check covers the
//   pass-through.
//
// Conservative: the swept AABB is the union of the start
//   and end bounds' axis-aligned bounding box. This is
//   strictly larger than the true swept volume (the latter
//   is a Minkowski sum), so it MAY reject a step that is
//   geometrically OK. The spec explicitly accepts this:
//   "第一版可以保守阻止部分本可通过的移动". The hard
//   requirement is "不得允许真实 footprint 重叠" which
//   the swept AABB does guarantee.
//
// Half-open AABB semantics match P4.4B: bounds are
// [min, max). The swept AABB is also half-open: the
// collision test `aabb_overlap` returns true iff the
// strict interiors intersect (i.e. shared interior area
//   or shared interior axis length).

#pragma once

#include "gamestate/component/api/footprint.h"


namespace openage::scenario {


/**
 * Compute the conservative swept AABB for a single segment
 * from `start` to `end`. Both bounds are interpreted as
 * half-open [min, max); the result is also half-open.
 *
 * Algorithm: union the two AABBs (take the per-axis min
 * and max). This is strictly larger than the true swept
 * volume but the only safe conservative choice that is
 * independent of the unit's orientation.
 */
inline openage::gamestate::component::FootprintBounds
swept_aabb(
	const openage::gamestate::component::FootprintBounds &start,
	const openage::gamestate::component::FootprintBounds &end) {
	openage::gamestate::component::FootprintBounds out;
	out.min_x = start.min_x < end.min_x ? start.min_x : end.min_x;
	out.min_y = start.min_y < end.min_y ? start.min_y : end.min_y;
	out.max_x = start.max_x > end.max_x ? start.max_x : end.max_x;
	out.max_y = start.max_y > end.max_y ? start.max_y : end.max_y;
	return out;
}


/**
 * Conservative segment check: returns true iff the swept
 * AABB of (start, end) overlaps the bounds of `obstacle`.
 * Half-open semantics on all sides.
 */
inline bool segment_overlaps(
	const openage::gamestate::component::FootprintBounds &start,
	const openage::gamestate::component::FootprintBounds &end,
	const openage::gamestate::component::FootprintBounds &obstacle) {
	auto swept = swept_aabb(start, end);
	return swept.min_x < obstacle.max_x
	    && obstacle.min_x < swept.max_x
	    && swept.min_y < obstacle.max_y
	    && obstacle.min_y < swept.max_y;
}


}  // namespace openage::scenario
