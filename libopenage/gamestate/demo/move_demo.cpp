// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#include "move_demo.h"

#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <eigen3/Eigen/Dense>

#include "coord/phys.h"
#include "coord/picking.h"
#include "coord/tile.h"
#include "cvar/cvar.h"
#include "event/event_loop.h"
#include "event/evententity.h"
#include "event/eventhandler.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/types.h"
#include "gamestate/event/send_command.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/manager.h"
#include "gamestate/simulation.h"
#include "gamestate/types.h"
#include "log/log.h"
#include "log/level.h"
#include "log/message.h"
#include "time/time.h"
#include "time/time_loop.h"
#include "util/fixed_point.h"
#include "util/vector.h"


namespace openage::gamestate::tests {

void simulation_demo_1(const util::Path &path) {
	log::log(MSG(info) << "========== MOVE COMMAND INTEGRATION TEST (demo 1) ==========");

	auto cvar = std::make_shared<cvar::CVarManager>(path);
	auto time_loop = std::make_shared<time::TimeLoop>();
	auto simulation = std::make_shared<GameSimulation>(
		path,
		cvar,
		time_loop,
		"engine.util.setup.tutorial_setup.TutorialPlayerSetup",
		"engine.util.scenario.mission_1.Mission1");
	simulation->set_modpacks({"engine"});

	log::log(MSG(info) << "Starting simulation (loads Mission 1 scenario, spawns villagers)...");
	simulation->start();
	log::log(MSG(info) << "Simulation started. Entities in state: "
	                   << simulation->get_game()->get_state()->get_game_entities().size());

	auto game = simulation->get_game();
	auto state = game->get_state();
	auto event_loop = simulation->get_event_loop();
	auto commander = simulation->get_commander();

	// Find the player villager: the scenario spawns PlayerVillager (tile 8,8)
	// which carries a Move ability. Buildings (town center) have no Move.
	auto &entities = state->get_game_entities();
	entity_id_t villager_id = 0;
	std::shared_ptr<GameEntity> villager;
	std::shared_ptr<component::Position> pos_comp;
	coord::phys3 start_pos = coord::tile{0, 0}.to_phys3();
	bool found = false;
	for (auto &[id, entity] : entities) {
		if (entity->has_component(component::component_t::MOVE)) {
			villager_id = id;
			villager = entity;
			pos_comp = std::dynamic_pointer_cast<component::Position>(
				entity->get_component(component::component_t::POSITION));
			start_pos = pos_comp->get_positions().get(time::time_t{0});
			found = true;
			break;
		}
	}

	if (not found) {
		log::log(MSG(err) << "FAIL: no entity with a Move component was spawned. "
		                  "Cannot test movement.");
		log::log(MSG(info) << "========== END MOVE TEST (no movable entity) ==========");
		return;
	}

	log::log(MSG(info) << "Found movable villager id=" << villager_id
	                   << " start_pos ne=" << start_pos.ne.to_double()
	                   << " se=" << start_pos.se.to_double()
	                   << " (tile ~"
	                   << start_pos.to_tile().ne << "," << start_pos.to_tile().se << ")");

	// HEADLESS: no renderer => attach_renderer() never runs, so the activity
	// is never kicked and primer_command_in_queue never creates the
	// process_command event / add_dependent on the command queue. Without
	// this, add_command's changes() has no dependent to notify and the move
	// command is silently dropped. Kick the activity once manually.
	log::log(MSG(info) << "Kicking activity at t=0 to build command-wait primer...");
	villager->get_manager()->run_activity_system(time::time_t{0});

	// Issue a MoveCommand to tile (10, 10) -- well inside the 16x16 map.
	auto dest = coord::tile{10, 10}.to_phys3();
	log::log(MSG(info) << "Issuing MoveCommand -> dest ne=" << dest.ne.to_double()
	                   << " se=" << dest.se.to_double()
	                   << " (tile 10,10)");

	openage::event::EventHandler::param_map::map_t params{
		{"type", component::command::command_t::MOVE},
		{"target", dest},
		{"entity_ids", std::vector<entity_id_t>{villager_id}},
	};
	event_loop->create_event(
		"game.send_command",
		commander,
		state,
		time::time_t{0},
		params);

	// Advance simulation time well past the move duration
	// (move_speed=3, distance ~2 tiles ~= 0.7s; 100s is plenty).
	log::log(MSG(info) << "Advancing simulation to t=100s...");
	event_loop->reach_time(time::time_t::from_int(100), state);

	auto final_pos = pos_comp->get_positions().get(time::time_t::from_int(100));
	log::log(MSG(info) << "After advance: final_pos ne=" << final_pos.ne.to_double()
	                   << " se=" << final_pos.se.to_double()
	                   << " (tile ~" << final_pos.to_tile().ne
	                   << "," << final_pos.to_tile().se << ")");

	bool moved = (final_pos.ne.get_raw_value() != start_pos.ne.get_raw_value())
	             or (final_pos.se.get_raw_value() != start_pos.se.get_raw_value());

	if (not moved) {
		log::log(MSG(err) << "FAIL: villager did NOT move. Position unchanged. "
		                  "Break is in the move chain (see find_path/move_default "
		                  "diagnostic logs above).");
	}
	else {
		auto delta = final_pos - dest;
		double dist = delta.length();
		// 1 tile in phys3 = 2^16 phys units (phys_t radix = 16). Report tiles.
		double dist_tiles = dist / 65536.0;
		log::log(MSG(info) << "PASS: villager MOVED. Distance to dest = "
		                   << dist << " phys (" << dist_tiles << " tiles)");
		if (dist_tiles > 1.0) {
			log::log(MSG(warn) << "WARN: moved but stopped >1 tile from dest "
			                   "(expected near 0). Path may be partial.");
		}
	}

	log::log(MSG(info) << "========== END MOVE TEST ==========");
}

void simulation_demo_2(const util::Path &path) {
	log::log(MSG(info) << "========== SELECTION + MOVE INTEGRATION TEST (demo 2) ==========");
	log::log(MSG(info) << "Reproduces the --tutorial right-click path headlessly: "
	                   "SELECTABLE comp -> drag_select -> selected ids -> "
	                   "send_command(MOVE) -> reach_time -> position change.");

	auto cvar = std::make_shared<cvar::CVarManager>(path);
	auto time_loop = std::make_shared<time::TimeLoop>();
	auto simulation = std::make_shared<GameSimulation>(
		path,
		cvar,
		time_loop,
		"engine.util.setup.tutorial_setup.TutorialPlayerSetup",
		"engine.util.scenario.mission_1.Mission1");
	simulation->set_modpacks({"engine"});
	simulation->start();

	auto game = simulation->get_game();
	auto state = game->get_state();
	auto event_loop = simulation->get_event_loop();
	auto commander = simulation->get_commander();

	auto &entities = state->get_game_entities();
	entity_id_t villager_id = 0;
	std::shared_ptr<GameEntity> villager;
	std::shared_ptr<component::Position> pos_comp;
	coord::phys3 start_pos = coord::tile{0, 0}.to_phys3();
	bool found = false;
	for (auto &[id, entity] : entities) {
		if (entity->has_component(component::component_t::MOVE)) {
			villager_id = id;
			villager = entity;
			pos_comp = std::dynamic_pointer_cast<component::Position>(
				entity->get_component(component::component_t::POSITION));
			start_pos = pos_comp->get_positions().get(time::time_t{0});
			found = true;
			break;
		}
	}
	if (not found) {
		log::log(MSG(err) << "FAIL: no movable entity spawned.");
		return;
	}

	// ---- CHECK 1: villager MUST have the SELECTABLE component. ----
	// This is the Phase 0.5 bug #13 root cause: without a Selectable ability
	// in the nyan, entity_factory never mounts SELECTABLE, and drag_select
	// skips the entity (drag_select.cpp `has_component(SELECTABLE)` guard).
	bool has_selectable = villager->has_component(component::component_t::SELECTABLE);
	log::log(MSG(info) << "CHECK 1: villager has SELECTABLE component = "
	                   << (has_selectable ? "YES" : "NO"));
	if (not has_selectable) {
		log::log(MSG(err) << "FAIL: villager lacks SELECTABLE. drag_select will "
		                  << "skip it -> get_selected() empty -> right-click move "
		                  << "sends empty entity_ids -> nothing moves.");
		log::log(MSG(info) << "========== END SELECTION + MOVE TEST ==========");
		return;
	}

	// Kick activity to build the command-wait primer (headless: no
	// attach_renderer). Same rationale as demo 1.
	villager->get_manager()->run_activity_system(time::time_t{0});

	// ---- CHECK 2: drag_select actually selects the villager. ----
	// Use an identity camera_matrix and a huge NDC rectangle so the
	// clip_pos test is guaranteed to pass -- we are testing the SELECTABLE +
	// ownership filtering, not the camera projection.
	std::vector<entity_id_t> selected;
	Eigen::Matrix4f cam_matrix = Eigen::Matrix4f::Identity();
	openage::event::EventHandler::param_map::map_t drag_params{
		{"controlled", static_cast<size_t>(0)},
		{"drag_start", Eigen::Vector2f{-1.0e7f, -1.0e7f}},
		{"drag_end", Eigen::Vector2f{1.0e7f, 1.0e7f}},
		{"camera_matrix", cam_matrix},
		{"select_cb",
	     std::function<void(const std::vector<entity_id_t>)>{
			 [&](const std::vector<entity_id_t> ids) { selected = ids; }}},
	};
	event_loop->create_event(
		"game.drag_select",
		commander,
		state,
		time::time_t{0},
		drag_params);
	// Execute the OnceEventHandler immediately at t=0 (it calls select_cb ->
	// controller->set_selected, but here we capture into `selected` directly).
	event_loop->reach_time(time::time_t{0}, state);

	log::log(MSG(info) << "CHECK 2: drag_select selected " << selected.size()
	                   << " entities");
	bool villager_selected = false;
	for (auto id : selected) {
		log::log(MSG(info) << "  selected id=" << id);
		if (id == villager_id) villager_selected = true;
	}
	if (not villager_selected) {
		log::log(MSG(err) << "FAIL: villager NOT selected by drag_select. "
		                  << "Check owner==controlled (owner filter) or clip_pos math.");
		log::log(MSG(info) << "========== END SELECTION + MOVE TEST ==========");
		return;
	}
	log::log(MSG(info) << "PASS: villager selected by drag_select");

	// ---- CHECK 3: right-click move via selected ids (like move_entity binding). ----
	// controller.cpp move_entity: entity_ids = controller->get_selected(). Here we
	// feed the drag_select result straight into send_command -- exactly what the
	// input binding does after a real right-click.
	auto dest = coord::tile{10, 10}.to_phys3();
	openage::event::EventHandler::param_map::map_t params{
		{"type", component::command::command_t::MOVE},
		{"target", dest},
		{"entity_ids", selected},
	};
	event_loop->create_event(
		"game.send_command",
		commander,
		state,
		time::time_t{0},
		params);
	event_loop->reach_time(time::time_t::from_int(100), state);

	auto final_pos = pos_comp->get_positions().get(time::time_t::from_int(100));
	log::log(MSG(info) << "final_pos ne=" << final_pos.ne.to_double()
	                   << " se=" << final_pos.se.to_double());

	bool moved = (final_pos.ne.get_raw_value() != start_pos.ne.get_raw_value())
	             or (final_pos.se.get_raw_value() != start_pos.se.get_raw_value());
	if (moved) {
		auto delta = final_pos - dest;
		double dist_tiles = delta.length() / 65536.0;
		log::log(MSG(info) << "PASS: villager MOVED via selection->send_command "
		                   << "chain. dist=" << dist_tiles << " tiles");
	}
	else {
		log::log(MSG(err) << "FAIL: villager did NOT move via selection chain.");
	}
	log::log(MSG(info) << "========== END SELECTION + MOVE TEST ==========");
}

void simulation_demo_3(const util::Path &/*path*/) {
	log::log(MSG(info) << "========== REAL-CAMERA PICKING TEST (demo 3) ==========");
	log::log(MSG(info) << "Tests screen_to_world_on_terrain with the tutorial's ACTUAL "
	                   << "camera params (no identity camera, no window). Outputs every "
	                   << "intermediate coordinate so the first picking error is visible.");

	// Tutorial camera, derived from presenter.cpp:121-122:
	//   Camera(renderer, window->get_size()); look_at_coord(scene3{10,10,0})
	// look_at_coord -> look_at_scene(world(10,0,-10)) -> calc_look_at:
	//   y_delta = 10, xz_distance = 10*sqrt(3) = 17.32, side = 17.32/sqrt(2) = 12.25
	//   new_pos = (10+12.25, 10, -10+12.25) = (22.25, 10, 2.25), within boundaries.
	Eigen::Vector3f camPos{22.25f, 10.0f, 2.25f};
	util::Vector2s viewport{1280, 720};
	float realZoom = 0.5f * (1.0f / 49.0f) * 1.0f;
	log::log(MSG(info) << "cam_scene_pos=(" << camPos.x() << ","
	                   << camPos.y() << "," << camPos.z()
	                   << ") viewport=" << viewport[0] << "x" << viewport[1]
	                   << " real_zoom=" << realZoom
	                   << " (=> ortho half-width = " << (640 * realZoom) << " tiles)");

	// Turn on DBG so screen_to_world_on_terrain prints ndc/ray_origin/t/hit per click.
	log::set_level(log::level::dbg);

	// CamelCase names (not pc/pb/etc) avoid bizarre "non-class type char" errors
	// triggered by Eigen SIMD packet locals sharing those short names.
	coord::scene3 resCenter = coord::screen_to_world_on_terrain(coord::input{640, 360},  viewport, camPos, realZoom);
	coord::scene3 resRight  = coord::screen_to_world_on_terrain(coord::input{1280, 360}, viewport, camPos, realZoom);
	coord::scene3 resLeft   = coord::screen_to_world_on_terrain(coord::input{0, 360},    viewport, camPos, realZoom);
	coord::scene3 resTop    = coord::screen_to_world_on_terrain(coord::input{640, 0},    viewport, camPos, realZoom);
	coord::scene3 resBottom = coord::screen_to_world_on_terrain(coord::input{640, 720},  viewport, camPos, realZoom);

	log::set_level(log::level::info);

	double centerNe = resCenter.ne.to_double();
	double centerSe = resCenter.se.to_double();
	double rightNe  = resRight.ne.to_double();
	double rightSe  = resRight.se.to_double();
	double leftNe   = resLeft.ne.to_double();
	double leftSe   = resLeft.se.to_double();
	double topNe    = resTop.ne.to_double();
	double bottomNe = resBottom.ne.to_double();

	log::log(MSG(info) << "RESULTS (scene3 == tile, since 1.0 phys3 = 1 tile):");
	log::log(MSG(info) << "  center -> (" << centerNe << "," << centerSe << ")");
	log::log(MSG(info) << "  right  -> (" << rightNe  << "," << rightSe  << ")");
	log::log(MSG(info) << "  left   -> (" << leftNe   << "," << leftSe   << ")");
	log::log(MSG(info) << "  top    -> (" << topNe    << ")");
	log::log(MSG(info) << "  bottom -> (" << bottomNe << ")");

	int pass = 0, fail = 0;

	if (std::abs(centerNe - 10.0) < 0.5 && std::abs(centerSe - 10.0) < 0.5) {
		log::log(MSG(info) << "PASS: center->focus  center=(" << centerNe << "," << centerSe << ") expect ~(10,10)");
		pass++;
	} else { log::log(MSG(err) << "FAIL: center->focus  center=(" << centerNe << "," << centerSe << ") expect ~(10,10)"); fail++; }

	if (rightSe > centerSe) { log::log(MSG(info) << "PASS: right->se+  right.se=" << rightSe << " > center.se=" << centerSe); pass++; }
	else { log::log(MSG(err) << "FAIL: right->se+  right.se=" << rightSe << " <= center.se=" << centerSe); fail++; }

	if (leftSe < centerSe) { log::log(MSG(info) << "PASS: left->se-  left.se=" << leftSe << " < center.se=" << centerSe); pass++; }
	else { log::log(MSG(err) << "FAIL: left->se-  left.se=" << leftSe << " >= center.se=" << centerSe); fail++; }

	if (topNe > centerNe) { log::log(MSG(info) << "PASS: top->ne+ (no Y flip)  top.ne=" << topNe << " > center.ne=" << centerNe); pass++; }
	else { log::log(MSG(err) << "FAIL: top->ne+ (no Y flip)  top.ne=" << topNe << " <= center.ne=" << centerNe); fail++; }

	if (bottomNe < centerNe) { log::log(MSG(info) << "PASS: bottom->ne-  bottom.ne=" << bottomNe << " < center.ne=" << centerNe); pass++; }
	else { log::log(MSG(err) << "FAIL: bottom->ne-  bottom.ne=" << bottomNe << " >= center.ne=" << centerNe); fail++; }

	auto resCenterBig = coord::screen_to_world_on_terrain(coord::input{1280, 720}, util::Vector2s{2560, 1440}, camPos, realZoom);
	double centerBigNe = resCenterBig.ne.to_double();
	double centerBigSe = resCenterBig.se.to_double();
	if (std::abs(centerBigNe - centerNe) < 0.001 && std::abs(centerBigSe - centerSe) < 0.001) {
		log::log(MSG(info) << "PASS: center viewport-invariant (DPR)  1280x720=(" << centerNe << "," << centerSe << ")  2560x1440=(" << centerBigNe << "," << centerBigSe << ")");
		pass++;
	} else { log::log(MSG(err) << "FAIL: center viewport-invariant (DPR)  1280x720=(" << centerNe << "," << centerSe << ")  2560x1440=(" << centerBigNe << "," << centerBigSe << ")"); fail++; }

	auto resRightBig = coord::screen_to_world_on_terrain(coord::input{2560, 720}, util::Vector2s{2560, 1440}, camPos, realZoom);
	double rightBigSe = resRightBig.se.to_double();
	if (rightBigSe > centerBigSe) { log::log(MSG(info) << "PASS: right still se+ at bigger viewport  right-of-big.se=" << rightBigSe << " > big-center.se=" << centerBigSe); pass++; }
	else { log::log(MSG(err) << "FAIL: right still se+ at bigger viewport  right-of-big.se=" << rightBigSe << " <= big-center.se=" << centerBigSe); fail++; }

	log::log(MSG(info) << "==== demo 3 summary: " << pass << " pass, " << fail << " fail ====");
	log::log(MSG(info) << "========== END REAL-CAMERA PICKING TEST ==========");
}

} // namespace openage::gamestate::tests
