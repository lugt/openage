// Copyright 2026 the openage authors. See copying.md for legal info.

#include "presenter/hud_qml_adapter.h"

#include "gamestate/game.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "presenter/viewmodel/mission_view.h"
#include "presenter/viewmodel/selected_entity_view.h"
#include "presenter/viewmodel/selection_model.h"
#include "scenario/trigger/mission_state.h"
#include "scenario/trigger/trigger_runtime.h"

#include <memory>
#include <string>


namespace openage::presenter {


HudQmlAdapter::HudQmlAdapter(QObject *parent) :
	QObject(parent) {}


HudQmlAdapter::~HudQmlAdapter() = default;


void HudQmlAdapter::attach(
	viewmodel::SelectionModel &model,
	openage::gamestate::GameSimulation &sim) {
	this->model_ = &model;
	// Store the simulation as a non-owning shared_ptr with a
	// no-op deleter so the adapter can hand a shared_ptr to
	// SelectedEntityView's constructor (which requires a
	// shared_ptr<GameState>). The Simulation is owned by the
	// caller; the adapter must not outlive it.
	this->sim_ = std::shared_ptr<openage::gamestate::GameSimulation>(
		&sim, [](auto *) {});

	model.subscribe_selection_changed(
		[this](openage::presenter::viewmodel::SelectionSnapshot new_snap,
		       openage::presenter::viewmodel::SelectionSnapshot /*old*/) {
			if (new_snap.primary.has_value()) {
				this->sel_id_ = *new_snap.primary;
			}
			else {
				this->sel_id_ = -1;
			}
			this->refresh_selection();
			emit this->selected_changed();
		});
	model.subscribe_health_changed(
		[this](openage::gamestate::entity_id_t id, qint64 new_hp) {
			if (this->sel_id_ == static_cast<qint64>(id)) {
				this->sel_hp_cur_ = new_hp;
				emit this->health_changed();
			}
		});
	model.subscribe_entity_removed(
		[this](openage::gamestate::entity_id_t /*id*/) {
			this->refresh_mission();
			emit this->mission_changed();
		});
	model.subscribe_mission_state_changed(
		[this](const std::string &reason) {
			this->ms_reason_ = QString::fromStdString(reason);
			this->refresh_mission();
			emit this->mission_changed();
		});

	this->refresh_selection();
	this->refresh_mission();
}


void HudQmlAdapter::refresh_selection() {
	if (not this->model_ || not this->sim_) return;
	auto sel_id = this->model_->selected_entity_id();
	if (not sel_id.has_value()) {
		this->sel_id_ = -1;
		this->sel_name_.clear();
		this->sel_owner_ = -1;
		this->sel_alive_ = false;
		this->sel_hp_cur_ = 0;
		this->sel_hp_max_ = 0;
		this->sel_atk_dmg_ = 0;
		this->sel_atk_rng_ = 0.0;
		this->sel_atk_cd_ = 0.0;
		this->sel_atk_active_ = false;
		return;
	}
	auto state = this->sim_->get_game()->get_state();
	if (not state) return;
	viewmodel::SelectedEntityView v{state, *sel_id};
	this->sel_id_ = static_cast<qint64>(*sel_id);
	this->sel_name_ = QString::fromStdString(
		v.scenario_object_id().value_or(""));
	this->sel_owner_ = static_cast<qint64>(v.owner_player_id());
	this->sel_alive_ = v.is_alive();
	this->sel_hp_cur_ = static_cast<qint64>(v.hp_current().value_or(0));
	this->sel_hp_max_ = static_cast<qint64>(v.hp_max().value_or(0));
	this->sel_atk_dmg_ = static_cast<qint64>(v.attack_damage().value_or(0));
	this->sel_atk_rng_ = static_cast<qreal>(v.attack_range().value_or(0.0));
	this->sel_atk_cd_ = static_cast<qreal>(v.attack_cooldown().value_or(0.0));
	this->sel_atk_active_ = v.attack_is_active();
}


void HudQmlAdapter::refresh_mission() {
	if (not this->sim_) return;
	auto &ms = this->sim_->get_trigger_runtime()->mission_state();
	viewmodel::MissionView mv{ms};
	this->ms_lifecycle_ = QString::fromStdString(mv.lifecycle());
	this->ms_victory_ = mv.is_victory();
	if (mv.is_victory()) {
		this->ms_reason_ = QString::fromStdString(mv.victory_reason());
	}
	for (const auto &o : mv.objectives()) {
		if (o.id == "mission_01.defeat_enemy") {
			this->ms_defeat_state_ = QString::fromStdString(o.state);
			break;
		}
	}
}


} // namespace openage::presenter