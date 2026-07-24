// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <eigen3/Eigen/Dense>

#include "coord/pixel.h"
#include "coord/scene.h"
#include "renderer/camera/definitions.h"
#include "util/vector.h"


namespace openage::coord {

/**
 * Unified screen-to-world-on-terrain picking.
 *
 * Maps a mouse position to a point on the terrain plane in scene3 coordinates.
 * This is the SINGLE source of truth for mouse picking: both left-click
 * drag-select (DragSelectHandler camera_matrix path) and right-click move
 * (input::to_phys3) must go through here so they cannot disagree.
 *
 * GL-free (takes plain camera parameters, not a Camera object) so it is
 * unit-testable headlessly without an OpenGL context. The Camera supplies the
 * same values at runtime via get_scene_pos() / get_viewport_size() /
 * get_real_zoom_factor().
 *
 * Pipeline (every step is logged in DEBUG by the implementation):
 *  1. Qt logical-pixel mouse -> NDC in [-1, 1], with screen-Y flipped
 *     (Qt y grows downward, NDC y grows upward).
 *  2. NDC -> world-space offset on the camera image plane, scaled by
 *     half-viewport * real_zoom (matches the orthographic projection matrix,
 *     which uses the same real_zoom * halfwidth / halfheight).
 *  3. ray_origin = camera eye + right*off_x + up*off_y  (point on image plane).
 *  4. Orthographic ray (constant direction == camera forward) intersected
 *     with the terrain plane y = terrain_y.
 *  5. world -> scene3 (inverse of scene3::to_world_space: world=(se, up*ratio, -ne)).
 *
 * @param mouse         Qt logical-pixel mouse position.
 * @param viewport_size Camera viewport in the SAME unit as mouse (logical px).
 *                      This must match the unit mouse uses -- mixing logical
 *                      mouse with physical viewport (or vice versa) is the
 *                      classic HiDPI picking bug.
 * @param cam_scene_pos Camera eye position in scene/world space.
 * @param real_zoom     Camera::get_real_zoom_factor()
 *                      (= 0.5 * default_zoom_ratio * zoom).
 * @param cam_dir       Camera forward direction (default CAM_DIRECTION).
 * @param cam_up        Camera up vector (default CAM_UP).
 * @param terrain_y     Terrain plane height (default 0).
 *
 * @return scene3 world point on the terrain plane under the cursor.
 */
scene3 screen_to_world_on_terrain(
	const input &mouse,
	const util::Vector2s &viewport_size,
	const Eigen::Vector3f &cam_scene_pos,
	float real_zoom,
	const Eigen::Vector3f &cam_dir = renderer::camera::CAM_DIRECTION,
	const Eigen::Vector3f &cam_up = renderer::camera::CAM_UP,
	float terrain_y = 0.0f);

} // namespace openage::coord
