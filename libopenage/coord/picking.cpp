// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#include "picking.h"

#include "log/log.h"
#include "log/message.h"


namespace openage::coord {

scene3 screen_to_world_on_terrain(
	const input &mouse,
	const util::Vector2s &viewport_size,
	const Eigen::Vector3f &cam_scene_pos,
	float real_zoom,
	const Eigen::Vector3f &cam_dir,
	const Eigen::Vector3f &cam_up,
	float terrain_y) {

	// ---- 1. camera basis (mirrors Camera::get_input_pos / get_view_matrix) ----
	auto direction = cam_dir.normalized();
	Eigen::Vector3f f = direction;                    // forward
	Eigen::Vector3f s = f.cross(cam_up); s.normalize(); // right
	Eigen::Vector3f u = s.cross(f);                    // up (recomputed, orthogonal)

	// ---- 2. mouse (logical px) -> NDC [-1,1] with Y flip ----
	float vw = static_cast<float>(viewport_size[0]);
	float vh = static_cast<float>(viewport_size[1]);
	float mx = static_cast<float>(mouse.x);
	float my = static_cast<float>(mouse.y);
	float ndc_x = 2.0f * mx / vw - 1.0f;
	// Qt/screen y grows downward; flip so +ndc_y == screen-up.
	float ndc_y = -(2.0f * my / vh - 1.0f);

	// ---- 3. NDC -> world offset on camera image plane ----
	// Same real_zoom * half-extent as the orthographic projection matrix, so
	// the picking plane and the render projection agree on scale.
	float halfwidth = vw / 2.0f;
	float halfheight = vh / 2.0f;
	float off_x = ndc_x * (halfwidth * real_zoom);
	float off_y = ndc_y * (halfheight * real_zoom);

	// ---- 4. ray origin: world point on the camera image plane ----
	Eigen::Vector3f ray_origin = cam_scene_pos + s * off_x + u * off_y;

	// ---- 5. orthographic ray vs terrain plane (y = terrain_y) ----
	// Ortho camera: every pixel's ray has direction == view direction. Intersect
	// ray_origin + direction*t with the plane y = terrain_y.
	Eigen::Vector3f plane_dir{0.0f, 1.0f, 0.0f};
	float denom = direction.dot(plane_dir);
	// If the camera is parallel to the terrain (denom ~ 0) picking is undefined;
	// the fixed AoE dimetric camera never is, but guard anyway.
	float t = (terrain_y - ray_origin.y()) / denom;
	Eigen::Vector3f p = ray_origin + direction * t;

	log::log(DBG << "picking: mouse=(" << mx << "," << my
	             << ") vp=" << vw << "x" << vh << " ndc=(" << ndc_x << "," << ndc_y
	             << ") off=(" << off_x << "," << off_y
	             << ") ray_origin=(" << ray_origin.x() << "," << ray_origin.y()
	             << "," << ray_origin.z() << ") t=" << t
	             << ") hit=(" << p.x() << "," << p.y() << "," << p.z() << ")");

	// ---- 6. world -> scene3 (inverse of scene3::to_world_space) ----
	// scene3::to_world_space = (se, up*up_ratio, -ne)  =>  ne = -world.z, se = world.x
	return scene3{-p.z(), p.x(), 0.0f};
}

} // namespace openage::coord
