// Copyright 2026 the openage authors. See copying.md for legal info.

#include "pathfinding/formation_planner.h"

#include "log/log.h"
#include "log/message.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>


namespace openage::pathfinding {


namespace {


constexpr double SEPARATION_MARGIN = 0.5;
constexpr double TRANSLATION_STEP = 1.0;


// Check whether the slot (centered at (cx, cy) with the
// given footprint) is a valid formation slot:
//   - inside map bounds
//   - no static obstacle overlap (cell-center test)
//   - no dynamic entity overlap (excluding the slot's own
//     entity, if `entity` != 0)
//   - no reservation overlap
bool slot_valid(
	double cx,
	double cy,
	double rx,
	double ry,
	openage::gamestate::entity_id_t entity,
	const openage::scenario::DynamicOccupancy &dynamic,
	const openage::gamestate::scenario::StaticOccupancyGrid
		&static_occ,
	int map_min_x,
	int map_min_y,
	int map_max_x,
	int map_max_y,
	const std::vector<openage::gamestate::component::FootprintBounds>
		&reservations) {
	openage::gamestate::component::FootprintBounds b;
	b.min_x = cx - rx;
	b.min_y = cy - ry;
	b.max_x = cx + rx;
	b.max_y = cy + ry;
	if (b.min_x < static_cast<double>(map_min_x)
	    or b.min_y < static_cast<double>(map_min_y)
	    or b.max_x > static_cast<double>(map_max_x)
	    or b.max_y > static_cast<double>(map_max_y)) {
		return false;
	}
	// Static obstacle test: every cell whose center lies
	// inside b must be clear. Conservative: use the static
	// grid's own is_blocked for each cell.
	const int x_lo = static_cast<int>(std::floor(b.min_x));
	const int x_hi = static_cast<int>(std::floor(b.max_x - 1e-9));
	const int y_lo = static_cast<int>(std::floor(b.min_y));
	const int y_hi = static_cast<int>(std::floor(b.max_y - 1e-9));
	for (int x = x_lo; x <= x_hi; ++x) {
		for (int y = y_lo; y <= y_hi; ++y) {
			if (static_occ.is_blocked(
				    openage::gamestate::scenario::CellPosition{
					    x, y})) {
				return false;
			}
		}
	}
	// Dynamic occupancy.
	if (entity != 0) {
		if (not dynamic.can_occupy(entity, b)) {
			return false;
		}
	}
	else {
		if (dynamic.has_blocker(0, b)) {
			return false;
		}
	}
	// Reservations.
	for (const auto &r : reservations) {
		if (r.min_x < b.max_x && b.min_x < r.max_x
		    && r.min_y < b.max_y && b.min_y < r.max_y) {
			return false;
		}
	}
	return true;
}


// Stable grid layout: rows, cols such that cols >= rows
// and rows * cols >= N. Ties broken by columns >= rows.
std::pair<int, int> grid_dims(int n) {
	if (n <= 0) return {0, 0};
	int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
	int rows = static_cast<int>(
		std::ceil(static_cast<double>(n) / static_cast<double>(cols)));
	return {rows, cols};
}


// Enumerate the (dx, dy) offsets in (y, x) lexicographic
// order across a rows*cols grid centred on (0, 0). The
// ordering makes the assignment deterministic.
std::vector<std::pair<int, int>> slot_offsets(int rows, int cols) {
	std::vector<std::pair<int, int>> out;
	out.reserve(static_cast<size_t>(rows * cols));
	for (int r = 0; r < rows; ++r) {
		for (int c = 0; c < cols; ++c) {
			int dy = r - (rows - 1) / 2;
			int dx = c - (cols - 1) / 2;
			out.push_back({dx, dy});
		}
	}
	return out;
}


// Translation attempts in fixed order. We try the
// centroid first, then a ring of offsets out to
// MAX_TRANSLATION_RADIUS tiles. Within each radius the
// offsets are visited in (y, x) lexicographic order. The
// planner picks the first one that yields enough valid slots.
constexpr int MAX_TRANSLATION_RADIUS = 6;

std::vector<std::pair<int, int>> translation_offsets() {
	std::vector<std::pair<int, int>> out;
	out.reserve(
		(2 * MAX_TRANSLATION_RADIUS + 1)
		* (2 * MAX_TRANSLATION_RADIUS + 1));
	for (int r = 0; r <= MAX_TRANSLATION_RADIUS; ++r) {
		for (int dy = -r; dy <= r; ++dy) {
			for (int dx = -r; dx <= r; ++dx) {
				if (r > 0 && std::max(std::abs(dx), std::abs(dy)) != r) {
					continue;
				}
				out.push_back({dx, dy});
			}
		}
	}
	return out;
}


}  // namespace


FormationPlan plan_formation(
	const std::vector<openage::gamestate::entity_id_t> &selection,
	double destination_x,
	double destination_y,
	double max_footprint_width,
	double max_footprint_height,
	const openage::scenario::DynamicOccupancy &dynamic,
	const openage::gamestate::scenario::StaticOccupancyGrid
		&static_occ,
	int map_min_x,
	int map_min_y,
	int map_max_x,
	int map_max_y,
	const std::vector<openage::gamestate::component::FootprintBounds>
		&reservations) {
	FormationPlan out;
	const int n = static_cast<int>(selection.size());
	if (n == 0) {
		out.result = FormationResult::OK;
		out.reason = "empty_selection";
		return out;
	}

	// Slot pitch: footprint + separation margin, with a
	// floor of 1 tile so even tiny footprints get a
	// readable grid.
	const double slot_dx = std::max(
		1.0, max_footprint_width + SEPARATION_MARGIN);
	const double slot_dy = std::max(
		1.0, max_footprint_height + SEPARATION_MARGIN);
	const double half_w = slot_dx / 2.0;
	const double half_h = slot_dy / 2.0;

	// Stable entity order: sort the selection ascending by
	// entity id. This is the (entity -> slot) pairing order.
	std::vector<openage::gamestate::entity_id_t> units = selection;
	std::sort(units.begin(), units.end());

	const auto dims = grid_dims(n);
	const int rows = dims.first;
	const int cols = dims.second;
	const auto offsets = slot_offsets(rows, cols);

	for (const auto &t : translation_offsets()) {
		const double cx = destination_x
		    + static_cast<double>(t.first) * TRANSLATION_STEP;
		const double cy = destination_y
		    + static_cast<double>(t.second) * TRANSLATION_STEP;

		// Walk the grid in (y, x) lexicographic order and
		// collect valid slots. Match them 1:1 with units
		// (also sorted ascending). If we get < n valid
		// slots, try the next translation.
		std::vector<std::pair<double, double>> valid_centres;
		valid_centres.reserve(offsets.size());
		for (const auto &o : offsets) {
			double sx = cx + static_cast<double>(o.first) * slot_dx;
			double sy = cy + static_cast<double>(o.second) * slot_dy;
			if (slot_valid(sx, sy, half_w, half_h, 0, dynamic,
			        static_occ, map_min_x, map_min_y,
			        map_max_x, map_max_y, reservations)) {
				valid_centres.push_back({sx, sy});
				if (valid_centres.size()
				    >= static_cast<size_t>(n)) {
					break;
				}
			}
		}
		if (valid_centres.size() < static_cast<size_t>(n)) {
			continue;
		}
		// Build assignments. Pair (unit_i, slot_i) in
		// ascending order. Per the spec's stable pairing:
		// we sort both lists ascending and pair.
		out.assignments.clear();
		for (int i = 0; i < n; ++i) {
			FormationAssignment a;
			a.entity = units[static_cast<size_t>(i)];
			a.slot_center_x = valid_centres[
				static_cast<size_t>(i)].first;
			a.slot_center_y = valid_centres[
				static_cast<size_t>(i)].second;
			a.bounds.min_x = a.slot_center_x - half_w;
			a.bounds.min_y = a.slot_center_y - half_h;
			a.bounds.max_x = a.slot_center_x + half_w;
			a.bounds.max_y = a.slot_center_y + half_h;
			out.assignments.push_back(a);
		}
		out.result = FormationResult::OK;
		out.reason = "ok";
		return out;
	}

	// No translation produced enough valid slots.
	out.result = FormationResult::NO_SPACE;
	out.reason = "no_translation_yields_enough_slots";
	return out;
}


} // namespace openage::pathfinding
