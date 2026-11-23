// Copyright 2026 the openage authors. See copying.md for legal info.

#include "presenter/hud_qml_adapter.h"

#include "gamestate/game.h"
#include "gamestate/game_state.h"
#include "gamestate/player.h"
#include "gamestate/simulation.h"
#include "presenter/viewmodel/mission_view.h"
#include "presenter/viewmodel/selected_entity_view.h"
#include "presenter/viewmodel/selection_model.h"
#include "scenario/trigger/mission_state.h"
#include "scenario/trigger/trigger_runtime.h"

#include <memory>
#include <QBuffer>
#include <string>
#include <utility>


namespace openage::presenter {


HudQmlAdapter::HudQmlAdapter(QObject *parent) :
	QObject(parent) {}


HudQmlAdapter::~HudQmlAdapter() = default;


void HudQmlAdapter::attach(
	viewmodel::SelectionModel &model,
	openage::gamestate::GameSimulation &sim) {
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
			this->refresh_selection(new_snap.primary);
		});
	model.subscribe_health_changed(
		[this](openage::gamestate::entity_id_t id, qint64 new_hp) {
			QMetaObject::invokeMethod(this, [this, id, new_hp] {
				if (this->sel_id_ == static_cast<qint64>(id)) {
					this->sel_hp_cur_ = new_hp;
					this->sel_alive_ = new_hp > 0;
					emit this->health_changed();
				}
			});
		});
	sim.get_trigger_runtime()->mission_state().set_objective_listener([this] {
		this->refresh_mission();
	});
	model.subscribe_mission_state_changed(
		[this](const std::string &) {
			this->refresh_mission();
		});

	this->refresh_selection(model.selected_entity_id());
	this->refresh_mission();
}


void HudQmlAdapter::refresh_selection(std::optional<gamestate::entity_id_t> entity_id) {
	std::optional<viewmodel::SelectedEntityView> view;
	if (entity_id) {
		view.emplace(this->sim_->get_game()->get_state(), *entity_id);
	}
	// Snapshot reads happen on the simulation's notifying thread. Only the
	// adapter's Qt thread updates the properties that QML reads.
	QMetaObject::invokeMethod(this, [this, view = std::move(view)] {
		this->sel_id_ = view ? static_cast<qint64>(view->entity_id()) : -1;
		this->sel_name_ = view ? QString::fromStdString(view->scenario_object_id().value_or("")) : QString{};
		this->sel_owner_ = view ? view->owner_player_id() : -1;
		this->sel_alive_ = view && view->is_alive();
		this->sel_hp_cur_ = view ? view->hp_current().value_or(0) : 0;
		this->sel_hp_max_ = view ? view->hp_max().value_or(0) : 0;
		this->sel_atk_dmg_ = view ? view->attack_damage().value_or(0) : 0;
		this->sel_atk_rng_ = view ? view->attack_range().value_or(0.0) : 0.0;
		this->sel_atk_cd_ = view ? view->attack_cooldown().value_or(0.0) : 0.0;
		this->sel_atk_active_ = view && view->attack_is_active();
		emit this->selected_changed();
		emit this->health_changed();
	});
}


void HudQmlAdapter::set_minimap(QImage image) {
	if (image == this->minimap_image) {
		return;
	}
	this->minimap_image = std::move(image);
	QBuffer buffer;
	buffer.open(QIODevice::WriteOnly);
	this->minimap_image.save(&buffer, "PNG");
	this->minimap_url = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(buffer.data().toBase64());
	emit this->map_changed();
}

void HudQmlAdapter::set_diplomacy(QVariantList players) {
	if (players != this->relations) {
		this->relations = std::move(players);
		emit this->diplomacy_changed();
	}
}

void HudQmlAdapter::set_selection_details(QVariantList cards, QVariantList actions, int construction,
                                          bool can_stop) {
	if (this->cards == cards and this->actions == actions and this->construction == construction
	    and this->stop_enabled == can_stop) {
		return;
	}
	this->cards = std::move(cards);
	this->actions = std::move(actions);
	this->construction = construction;
	this->stop_enabled = can_stop;
	emit this->selection_details_changed();
}

void HudQmlAdapter::set_action_status(QString status) {
	if (this->status != status) {
		this->status = std::move(status);
		emit this->action_status_changed();
	}
}

void HudQmlAdapter::toggleMapUnits() {
	this->show_units = not this->show_units;
	emit this->map_options_changed();
}

void HudQmlAdapter::toggleMapViewport() {
	this->show_viewport = not this->show_viewport;
	emit this->map_options_changed();
}

bool HudQmlAdapter::is_interactive(qreal x, qreal y, qreal width, qreal height) const {
	const auto objective_top = width < this->economy_bar_width() + 316 ? this->economy_bar_height() + 20 : 12;
	const auto diplomacy_top = width < 534 ? objective_top + 118 : this->economy_bar_height() + 20;
	const bool map_and_commands = y >= height - this->command_bar_height() and y < height;
	const bool economy_bar = x >= 12 and x < 12 + this->economy_bar_width()
	                      and x < width - 12 and y >= 12 and y < 12 + this->economy_bar_height();
	const bool diplomacy = not this->relations.isEmpty()
	                    and x >= 12 and x < 242 and x < width - 12
	                    and y >= diplomacy_top and y < diplomacy_top + 32 + 28 * this->relations.size();
	const bool objectives = x >= width - 292 and x >= 12 and x < width - 12
	                     and y >= objective_top and y < objective_top + 110;
	return map_and_commands or economy_bar or diplomacy or objectives;
}

void HudQmlAdapter::refresh_economy(gamestate::player_id_t player) {
	const auto snapshot = this->sim_->get_game()->get_state()->player_economy(player);
	if (snapshot != this->economy) {
		this->economy = snapshot;
		emit this->economy_changed();
	}
}

void HudQmlAdapter::refresh_mission() {
	viewmodel::MissionView view{this->sim_->get_trigger_runtime()->mission_state()};
	QString resources;
	QString route;
	for (const auto &objective : view.objectives()) {
		if (objective.id == "mission_01.march") {
			route = objective.state == "COMPLETED"
			        ? tr("Destroy the outpost, then lead the scout along the road to the village.")
			        : tr("Lead the scout to the first bend in the road.");
		}
		if (objective.id == "mission_02.stockpile") {
			const auto player = this->sim_->get_game()->get_state()->get_player(0);
			resources = tr("Food %1/50   Wood %2/50   Gold %3/50")
			                .arg(player->get_resource("de2_base.data.util.resource.types.Food"))
			                .arg(player->get_resource("de2_base.data.util.resource.types.Wood"))
			                .arg(player->get_resource("de2_base.data.util.resource.types.Gold"));
			break;
		}
	}
    const auto &instruction = this->sim_->get_trigger_runtime()->mission_state().instruction();
    if (not instruction.empty()) route = QString::fromStdString(instruction);
	QMetaObject::invokeMethod(this, [this, view = std::move(view), resources, route] {
		this->ms_route_ = route;
		this->ms_stockpile_ = resources;
		this->ms_lifecycle_ = QString::fromStdString(view.lifecycle());
		this->ms_victory_ = view.is_victory();
		this->ms_reason_ = QString::fromStdString(view.victory_reason());
		this->ms_defeat_state_ = QStringLiteral("INACTIVE");
		for (const auto &objective : view.objectives()) {
			if (objective.id == "mission_01.defeat_enemy") {
				this->ms_defeat_state_ = QString::fromStdString(objective.state);
				break;
			}
		}
		emit this->mission_changed();
	});
}


} // namespace openage::presenter
