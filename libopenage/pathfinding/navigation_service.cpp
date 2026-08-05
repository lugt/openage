// Copyright 2026 the openage authors. See copying.md for legal info.

#include "pathfinding/navigation_service.h"

#include "log/log.h"
#include "log/message.h"

#include "gamestate/component/api/footprint.h"
#include "pathfinding/grid_astar.h"
#include "scenario/static_occupancy_grid.h"


namespace openage::pathfinding {


NavigationService::NavigationService(
	std::shared_ptr<openage::gamestate::scenario::NavigationGrid> grid,
	std::vector<openage::gamestate::component::FootprintBounds>
		static_obstacles) :
	grid_{std::move(grid)},
	static_obstacles_{std::move(static_obstacles)} {}


NavigationService::Path NavigationService::request_path(
	openage::gamestate::scenario::CellPosition start,
	openage::gamestate::scenario::CellPosition target,
	MoverFootprint mover) const {
	Path out;
	GridAStarPathfinder finder{
		this->grid_.get(),
		this->static_obstacles_,
		mover};
	out.result = finder.find_path(start, target, out.waypoints);
	if (out.result != GridAStarPathfinder::Result::PATH_FOUND) {
		// Per spec §VI: any failure clears the waypoints so the
		// Move runtime cannot accidentally traverse a partial
		// path. The runtime treats the empty waypoint vector
		// as "no path" and surfaces a MovementArrivedEvent with
		// status=FAILED.
		out.waypoints.clear();
		openage::log::log(MSG(info) << "NavigationService::request_path: "
		                            << "FAIL start=(" << start.x << "," << start.y
		                            << ") target=(" << target.x << "," << target.y
		                            << ") mover=(" << mover.radius_x << ","
		                            << mover.radius_y << ") obstacles="
		                            << this->static_obstacles_.size()
		                            << " result=" << static_cast<int>(out.result));
	}
	return out;
}


} // namespace openage::pathfinding
