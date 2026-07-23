// Copyright 2018-2023 the openage authors. See copying.md for legal info.

#include "world.h"

#include <unordered_map>
#include <utility>

#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/manager.h"
#include "renderer/render_factory.h"
#include "time/time.h"


namespace openage::gamestate {

World::World(const std::shared_ptr<GameState> &state) :
	state{state} {
	// TODO
}

void World::attach_renderer(const std::shared_ptr<renderer::RenderFactory> &render_factory) {
	this->render_factory = render_factory;

	for (auto entity_ref : state->get_game_entities()) {
		auto world_render_entity = this->render_factory->add_world_render_entity();
		entity_ref.second->set_render_entity(world_render_entity);

		// Run the entity's activity once (start -> idle node) now that its
		// render entity is attached. Idle::idle checks the ability's ANIMATED
		// property and calls GameEntity::render_update, which pushes the
		// current position + idle animation onto the render entity. Without
		// this the render entity stays empty and WorldObject::fetch_updates
		// draws nothing (entities were invisible). This mirrors what the
		// spawn_entity event does for runtime-spawned entities. t=0 matches
		// the scenario spawn time used by Game::load_scenario.
		entity_ref.second->get_manager()->run_activity_system(time::time_t{0});
	}
}


} // namespace openage::gamestate
