// Copyright 2026 the openage authors. See copying.md for legal info.

#include "gamestate/component/api/footprint.h"


namespace openage::gamestate::component {


Footprint Footprint::make_rectangle(
	double radius_x_tiles,
	double radius_y_tiles,
	bool blocks_placement,
	bool blocks_pathfinding,
	bool is_static) {

	// A 0-extent footprint is degenerate (entity is a point);
	// the validator catches this as INVALID_FOOTPRINT, and the
	// factory clamps negatives to 0 here. This class no longer
	// extends Component (see footprint.h for the rationale), so
	// the default ctor + member initializers are sufficient.
	Footprint f;
	f.shape_ = FootprintShape::AXIS_ALIGNED_RECTANGLE;
	f.radius_x_ = radius_x_tiles > 0.0 ? radius_x_tiles : 0.0;
	f.radius_y_ = radius_y_tiles > 0.0 ? radius_y_tiles : 0.0;
	f.blocks_placement_ = blocks_placement;
	f.blocks_pathfinding_ = blocks_pathfinding;
	f.is_static_ = is_static;
	// bounds_ stays default (all 0) until set_center.
	return f;
}


} // namespace openage::gamestate::component
