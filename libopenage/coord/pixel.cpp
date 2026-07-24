// Copyright 2016-2024 the openage authors. See copying.md for legal info.

#include "pixel.h"

#include "coord/phys.h"
#include "coord/picking.h"
#include "renderer/camera/camera.h"
#include "renderer/camera/definitions.h"


namespace openage {
namespace coord {

viewport_delta camhud_delta::to_viewport() const {
	return viewport_delta{this->x, this->y};
}


viewport camhud::to_viewport() const {
	// reverse of viewport::to_camhud
	return (*this - camhud{0, 0}).to_viewport() + viewport{0, 0};
}


camhud viewport::to_camhud() const {
	return camhud{0, 0} + (*this - viewport{0, 0}).to_camhud();
}


Eigen::Vector2f viewport::to_ndc_space(const std::shared_ptr<renderer::camera::Camera> &camera) const {
	return Eigen::Vector2f(
		2.0f * this->x / camera->get_viewport_size()[0] - 1,
		2.0f * this->y / camera->get_viewport_size()[1] - 1);
}


viewport_delta input_delta::to_viewport(const std::shared_ptr<renderer::camera::Camera> &camera) const {
	viewport_delta result;

	result.x = this->x;
	result.y = camera->get_viewport_size()[1] - this->y;

	return result;
}


viewport input::to_viewport(const std::shared_ptr<renderer::camera::Camera> &camera) const {
	return viewport{0, 0} + (*this - input{0, 0}).to_viewport(camera);
}


phys3 input::to_phys3(const std::shared_ptr<renderer::camera::Camera> &camera) const {
	return this->to_scene3(camera).to_phys3();
}


scene3 input::to_scene3(const std::shared_ptr<renderer::camera::Camera> &camera) const {
	// Single source of truth: see coord/picking.h. Both drag-select and
	// right-click move go through screen_to_world_on_terrain so they cannot
	// disagree on NDC, Y-flip, ray/terrain intersection, or world->scene3.
	return screen_to_world_on_terrain(
		*this,
		camera->get_viewport_size(),
		camera->get_scene_pos(),
		camera->get_real_zoom_factor());
}

} // namespace coord
} // namespace openage
