// Copyright 2023-2023 the openage authors. See copying.md for legal info.

#include "drag_select.h"

#include <eigen3/Eigen/Dense>
#include <algorithm>
#include <limits>

#include "coord/phys.h"
#include "coord/pixel.h"
#include "coord/scene.h"
#include "curve/discrete.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/types.h"


namespace openage::gamestate::event {

DragSelectHandler::DragSelectHandler() :
	OnceEventHandler{"game.drag_select"} {}

void DragSelectHandler::setup_event(const std::shared_ptr<openage::event::Event> & /* event */,
                                    const std::shared_ptr<openage::event::State> & /* state */) {
	// TODO
}

void DragSelectHandler::invoke(openage::event::EventLoop & /* loop */,
                               const std::shared_ptr<openage::event::EventEntity> & /* target */,
                               const std::shared_ptr<openage::event::State> &state,
                               const time::time_t &time,
                               const param_map &params) {
	auto gstate = std::dynamic_pointer_cast<openage::gamestate::GameState>(state);

	const auto controlled_id = params.get("controlled", player_id_t{});

	using Selection = std::vector<entity_id_t>;
	const auto camera = params.get<Eigen::Matrix4f>("camera_matrix", Eigen::Matrix4f::Identity());
	const auto start = params.get("drag_start", Eigen::Vector2f{0, 0});
	const auto end = params.get("drag_end", Eigen::Vector2f{0, 0});
	const auto sprite = params.get("sprite_pick", std::optional<uint32_t>{});
	const bool point = params.get("point_select", start == end);
	const bool same_type = point and params.get("same_type", false);

	Selection selected;
	Selection on_screen;
	std::optional<entity_id_t> picked;
	float closest = std::numeric_limits<float>::infinity();
	for (const auto &[id, entity] : gstate->get_game_entities()) {
		if (entity->is_dead() or not entity->has_component(component::component_t::SELECTABLE)
		    or not gstate->can_see(controlled_id, id)) {
			continue;
		}
		auto position = entity->try_get_component_as<component::Position>(component::component_t::POSITION);
		if (not position) {
			continue;
		}
		const auto world = position->get_positions().get(time).to_scene3().to_world_space();
		const Eigen::Vector4f clip = camera * Eigen::Vector4f{world.x(), world.y(), world.z(), 1};
		const Eigen::Vector2f screen = clip.head<2>() / clip.w();
		auto ownership = entity->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
		const bool own = ownership and ownership->get_owners().get(time) == controlled_id;

		if (point) {
			if (same_type and own and screen.cwiseAbs().maxCoeff() <= 1
			    and screen.y() >= params.get("screen_bottom", -1.0f)) {
				on_screen.push_back(id);
			}
			const auto delta = screen - end;
			const float distance = delta.squaredNorm();
			if ((sprite and id == *sprite)
			    or (not sprite and delta.cwiseAbs().maxCoeff() <= 0.05f
			        and (not picked or distance < closest or (distance == closest and id < *picked)))) {
				picked = id;
				closest = distance;
			}
		}
		else if (own and (screen.array() >= start.cwiseMin(end).array()).all()
		         and (screen.array() <= start.cwiseMax(end).array()).all()) {
			selected.push_back(id);
		}
	}

	if (picked) {
		selected.push_back(*picked);
		if (same_type) {
			const auto &definition = gstate->get_game_entity(*picked)->definition;
			auto ownership = gstate->get_game_entity(*picked)->try_get_component_as<component::Ownership>(
				component::component_t::OWNERSHIP);
			if (ownership and ownership->get_owners().get(time) == controlled_id and not definition.empty()) {
				for (auto id : on_screen) {
					if (gstate->get_game_entity(id)->definition == definition) {
						selected.push_back(id);
					}
				}
			}
		}
	}

	const auto is_building = [&](entity_id_t id) {
		return gstate->get_game_entity(id)->has_component(component::component_t::BUILD);
	};
	if (std::ranges::any_of(selected, [&](auto id) { return not is_building(id); })) {
		std::erase_if(selected, is_building);
	}
	if (params.get("additive", false)) {
		auto previous = params.get("previous_selection", std::function<Selection()>{});
		if (previous) {
			auto existing = previous();
			std::erase_if(existing, [&](auto id) {
				return not gstate->has_game_entity(id) or gstate->get_game_entity(id)->is_dead()
				    or not gstate->can_see(controlled_id, id);
			});
			if (not existing.empty()) {
				const bool buildings = is_building(existing.front());
				std::erase_if(selected, [&](auto id) { return is_building(id) != buildings; });
				selected.insert(selected.end(), existing.begin(), existing.end());
			}
		}
	}
	std::ranges::sort(selected);
	selected.erase(std::unique(selected.begin(), selected.end()), selected.end());

	// Select the units
	auto select_cb = params.get("select_cb",
	                            std::function<void(const std::vector<entity_id_t> ids)>{
									[](const std::vector<entity_id_t> /* ids */) {}});
	select_cb(selected);
}

time::time_t DragSelectHandler::predict_invoke_time(const std::shared_ptr<openage::event::EventEntity> & /* target */,
                                                    const std::shared_ptr<openage::event::State> & /* state */,
                                                    const time::time_t &at) {
	return at;
}


} // namespace openage::gamestate::event
