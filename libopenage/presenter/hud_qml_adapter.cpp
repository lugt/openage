// Copyright 2026 the openage authors. See copying.md for legal info.

#include "presenter/hud_qml_adapter.h"

#include "error/error.h"
#include "gamestate/chat_console.h"
#include "gamestate/game.h"
#include "gamestate/game_state.h"
#include "gamestate/player.h"
#include "gamestate/save_load.h"
#include "gamestate/simulation.h"
#include "time/clock.h"
#include "time/time_loop.h"
#include "presenter/viewmodel/mission_view.h"
#include "presenter/viewmodel/selected_entity_view.h"
#include "presenter/viewmodel/selection_model.h"
#include "scenario/trigger/mission_state.h"
#include "scenario/trigger/trigger_runtime.h"

#include <memory>
#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
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
	this->chat_console_ = std::make_unique<gamestate::ChatConsole>(sim, 0);

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
	if (const auto &runtime = sim.get_trigger_runtime()) {
		runtime->mission_state().set_objective_listener([this] {
			this->refresh_mission();
		});
	}
	model.subscribe_mission_state_changed(
		[this](const std::string &) {
			this->refresh_mission();
		});

	this->refresh_selection(model.selected_entity_id());
	this->refresh_mission();
}


void HudQmlAdapter::refresh_selection(std::optional<gamestate::entity_id_t> entity_id) {
	Is_True(this->sim_ != nullptr, ("HudQmlAdapter::refresh_selection: sim_ is null"));
	std::optional<viewmodel::SelectedEntityView> view;
	if (entity_id) {
		const auto game = this->sim_->get_game();
		Is_True(game != nullptr and game->get_state() != nullptr,
		        ("HudQmlAdapter::refresh_selection: game or state is null"));
		view.emplace(game->get_state(), *entity_id);
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
                                          bool can_stop, QVariantMap command_icons) {
	if (this->cards == cards and this->actions == actions and this->construction == construction
	    and this->stop_enabled == can_stop and this->command_images == command_icons) {
		return;
	}
	QVariantList previous_ids;
	QVariantList current_ids;
	for (const auto &card : this->cards) {
		previous_ids.append(card.toMap().value("id"));
	}
	for (const auto &card : cards) {
		current_ids.append(card.toMap().value("id"));
	}
	if (previous_ids != current_ids) {
		this->command_page_.clear();
	}
	this->cards = std::move(cards);
	this->command_images = std::move(command_icons);
	this->actions = std::move(actions);
	if (not this->command_page_.isEmpty()) {
		bool has_page = false;
		for (const auto &action : this->actions) {
			has_page = has_page or action.toMap().value("category").toString() == this->command_page_;
		}
		if (not has_page) {
			this->command_page_.clear();
		}
	}
	this->construction = construction;
	this->stop_enabled = can_stop;
	emit this->selection_details_changed();
}

QVariantList HudQmlAdapter::command_options() const {
	QVariantList result;
	QVariantMap groups;
	for (const auto &entry : this->actions) {
		const auto action = entry.toMap();
		if (not action.value("build").toBool()) {
			if (this->command_page_.isEmpty()) {
				result.append(action);
			}
			continue;
		}
		const auto category = action.value("category").toString();
		if (category.isEmpty()) {
			// Actions without a declared category remain accessible.
			if (this->command_page_.isEmpty()) {
				result.append(action);
			}
		}
		else if (this->command_page_ == category) {
			result.append(action);
		}
		else if (this->command_page_.isEmpty() and not groups.contains(category)) {
			groups.insert(category, QVariantMap{
				{"page", category}, {"name", category == "economic" ? tr("Economic buildings") : tr("Military buildings")},
				{"icon", action.value("icon")}, {"navigation", true},
			});
		}
	}
	if (this->command_page_.isEmpty()) {
		QVariantList navigation;
		for (const auto &category : {QStringLiteral("economic"), QStringLiteral("military")}) {
			if (groups.contains(category)) {
				navigation.append(groups.value(category));
			}
		}
		navigation.append(result);
		result = std::move(navigation);
	}
	else {
		result.append(QVariantMap{{"page", ""}, {"name", tr("Back")}, {"navigation", true}, {"back", true}});
	}
	return result;
}

bool HudQmlAdapter::activateCommand(int index) {
	const auto commands = this->command_options();
	if (index < 0 or index >= commands.size()) {
		return false;
	}
	const auto command = commands[index].toMap();
	if (command.value("back").toBool()) {
		return false;
	}
	if (command.value("navigation").toBool()) {
		this->command_page_ = command.value("page").toString();
		emit this->selection_details_changed();
	}
	else {
		this->requestAction(command.value("id").toString(), command.value("build").toBool());
	}
	return true;
}

bool HudQmlAdapter::backCommands() {
	if (this->command_page_.isEmpty()) {
		return false;
	}
	this->command_page_.clear();
	emit this->selection_details_changed();
	return true;
}

void HudQmlAdapter::set_production_queue(QVariantList queue) {
	if (this->prod_queue_ != queue) {
		this->prod_queue_ = std::move(queue);
		emit this->production_queue_changed();
	}
}

void HudQmlAdapter::set_action_status(QString status) {
	if (this->status != status) {
		this->status = std::move(status);
		emit this->action_status_changed();
	}
}

void HudQmlAdapter::show_toast(QString message) {
	if (message.isEmpty()) {
		return;
	}
	this->toast_message_ = std::move(message);
	++this->toast_seq_;
	emit this->toast_changed();
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
	if (this->menu_open_) {
		return true;
	}
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
	const bool chat_box = this->chat_open_
	                   and x >= 12 and x < std::min<qreal>(540, width - 12)
	                   and y >= height - this->command_bar_height() - 220
	                   and y < height - this->command_bar_height();
	const bool menu_btn = x >= 12 + this->economy_bar_width() + 8 and x < 12 + this->economy_bar_width() + 104
	                   and y >= 12 and y < 12 + 32;
	const bool leaderboard = not this->ranking.isEmpty() and x >= width - 252 and x < width - 12
	                      and y >= height - this->command_bar_height() - 44 - 26 * this->ranking.size()
	                      and y < height - this->command_bar_height() - 12;
	return map_and_commands or economy_bar or diplomacy or objectives or chat_box or menu_btn or leaderboard;
}

void HudQmlAdapter::openChat() {
	if (not this->chat_open_) {
		this->chat_open_ = true;
		emit this->chat_open_changed();
	}
}

void HudQmlAdapter::closeChat() {
	if (this->chat_open_) {
		this->chat_open_ = false;
		emit this->chat_open_changed();
	}
}

void HudQmlAdapter::pasteToChat() {
	if (const auto *clipboard = QGuiApplication::clipboard()) {
		const QString text = clipboard->text();
		if (not text.isEmpty()) {
			emit this->chat_paste_requested(text);
		}
	}
}

void HudQmlAdapter::submitChat(const QString &text) {
	if (not this->chat_console_) {
		this->closeChat();
		return;
	}
	const auto result = this->chat_console_->execute(text.toStdString(), 0);
	QVariantList next;
	const auto history = this->chat_console_->get_history();
	const size_t start = history.size() > 8 ? history.size() - 8 : 0;
	for (size_t i = start; i < history.size(); ++i) {
		const auto &entry = history[i];
		QString color = QStringLiteral("#e8edf2");
		if (entry.kind == gamestate::ChatEntryKind::CHEAT) {
			color = QStringLiteral("#ffd166");
		}
		else if (entry.kind == gamestate::ChatEntryKind::SYSTEM) {
			color = QStringLiteral("#7dd3fc");
		}
		else if (entry.kind == gamestate::ChatEntryKind::PYTHON) {
			color = QStringLiteral("#a7f3d0");
		}
		else if (entry.kind == gamestate::ChatEntryKind::ERROR) {
			color = QStringLiteral("#fca5a5");
		}
		QVariantMap row;
		row.insert(QStringLiteral("sender"), QString::fromStdString(entry.sender_name));
		row.insert(QStringLiteral("text"), QString::fromStdString(entry.text));
		row.insert(QStringLiteral("color"), color);
		next.push_back(std::move(row));
	}
	this->chat_messages_ = std::move(next);
	emit this->chat_messages_changed();
	if (not result.response.empty()) {
		this->set_action_status(QString::fromStdString(result.response));
	}
	if (this->sim_ and this->sim_->get_game() and this->sim_->get_game()->get_state()) {
		this->refresh_economy(0);
		this->refresh_mission();
	}
	this->closeChat();
}

bool HudQmlAdapter::fog_enabled() const {
	if (this->sim_ and this->sim_->get_game() and this->sim_->get_game()->get_state()) {
		return not this->sim_->get_game()->get_state()->is_no_fog_cheat();
	}
	return true;
}

void HudQmlAdapter::openMenu() {
	if (not this->menu_open_) {
		this->menu_open_ = true;
		this->menu_page_ = QStringLiteral("main");
		this->menu_status_.clear();
		if (this->sim_ and this->sim_->get_time_loop() and this->sim_->get_time_loop()->get_clock()) {
			auto clock = this->sim_->get_time_loop()->get_clock();
			const qreal cur = clock->get_speed().to_double();
			if (cur > 0.0) {
				this->pre_menu_speed_ = cur;
				this->game_speed_ = cur;
			}
			clock->set_speed(time::speed_t::from_double(0.0));
		}
		this->refreshSaves();
		emit this->menu_page_changed();
		emit this->menu_status_changed();
		emit this->settings_changed();
		emit this->menu_open_changed();
	}
}

void HudQmlAdapter::closeMenu() {
	if (this->menu_open_) {
		this->menu_open_ = false;
		if (this->sim_ and this->sim_->get_time_loop() and this->sim_->get_time_loop()->get_clock()) {
			this->sim_->get_time_loop()->get_clock()->set_speed(
				time::speed_t::from_double(this->pre_menu_speed_ > 0.0 ? this->pre_menu_speed_ : 1.0));
		}
		emit this->menu_open_changed();
	}
}

void HudQmlAdapter::toggleMenu() {
	if (this->menu_open_) {
		this->closeMenu();
	}
	else {
		this->openMenu();
	}
}

void HudQmlAdapter::setMenuPage(const QString &page) {
	if (this->menu_page_ != page) {
		this->menu_page_ = page;
		this->menu_status_.clear();
		if (page == QLatin1String("saveload")) {
			this->refreshSaves();
		}
		emit this->menu_page_changed();
		emit this->menu_status_changed();
	}
}

void HudQmlAdapter::refreshSaves() {
	QVariantList list;
	for (const auto &slot : gamestate::GameSaveManager::list_saves(this->saves_dir_)) {
		list.append(QVariantMap{
			{QStringLiteral("name"), QString::fromStdString(slot.name)},
			{QStringLiteral("summary"), QString::fromStdString(slot.summary)},
		});
	}
	this->save_slots_ = std::move(list);
	emit this->save_slots_changed();
}

void HudQmlAdapter::saveGame(const QString &slotName) {
	if (not this->sim_) {
		return;
	}
	QString clean = slotName.trimmed();
	if (clean.isEmpty()) {
		clean = QStringLiteral("quicksave");
	}
	for (QChar &ch : clean) {
		if (not ch.isLetterOrNumber() and ch != QLatin1Char('_') and ch != QLatin1Char('-')) {
			ch = QLatin1Char('_');
		}
	}
	const std::string path = this->saves_dir_ + "/" + clean.toStdString() + ".oasave";
	std::string err;
	if (gamestate::GameSaveManager::save_to_file(*this->sim_, path, &err)) {
		this->menu_status_ = QStringLiteral("Saved: %1").arg(clean);
		this->refreshSaves();
	}
	else {
		this->menu_status_ = QStringLiteral("Save failed: %1").arg(QString::fromStdString(err));
	}
	emit this->menu_status_changed();
}

void HudQmlAdapter::loadGame(const QString &slotName) {
	if (not this->sim_) {
		return;
	}
	const QString clean = slotName.trimmed().isEmpty() ? QStringLiteral("quicksave") : slotName.trimmed();
	const std::string path = this->saves_dir_ + "/" + clean.toStdString() + ".oasave";
	std::string err;
	if (gamestate::GameSaveManager::load_from_file(*this->sim_, path, &err)) {
		this->menu_status_ = QStringLiteral("Loaded: %1").arg(clean);
		this->refresh_economy(0);
		this->refresh_mission();
		emit this->game_loaded();
		emit this->settings_changed();
		this->closeMenu();
	}
	else {
		this->menu_status_ = QStringLiteral("Load failed: %1").arg(QString::fromStdString(err));
	}
	emit this->menu_status_changed();
}

void HudQmlAdapter::setGameSpeed(qreal speed) {
	const qreal clamped = std::clamp(speed, 0.5, 4.0);
	this->game_speed_ = clamped;
	this->pre_menu_speed_ = clamped;
	if (not this->menu_open_ and this->sim_ and this->sim_->get_time_loop() and this->sim_->get_time_loop()->get_clock()) {
		this->sim_->get_time_loop()->get_clock()->set_speed(time::speed_t::from_double(clamped));
	}
	emit this->settings_changed();
}

void HudQmlAdapter::toggleFogSetting() {
	if (this->sim_ and this->sim_->get_game() and this->sim_->get_game()->get_state()) {
		auto state = this->sim_->get_game()->get_state();
		state->set_no_fog_cheat(not state->is_no_fog_cheat());
		const auto now = this->sim_->get_time_loop() ? this->sim_->get_time_loop()->get_clock()->get_time() : time::time_t{0};
		state->update_visibility(now);
		emit this->settings_changed();
	}
}

void HudQmlAdapter::set_idle_villagers(int count) {
	if (this->idle_villagers_ == count) {
		return;
	}
	this->idle_villagers_ = count;
	emit this->idle_villagers_changed();
}

void HudQmlAdapter::refresh_economy(gamestate::player_id_t player) {
	const auto state = this->sim_->get_game()->get_state();
	QVariantList rows;
	for (const auto &score : state->player_scores()) {
		rows.push_back(QVariantMap{
			{"player", static_cast<qulonglong>(score.player)},
			{"name", QString::fromStdString(score.name)},
			{"score", static_cast<qlonglong>(score.total)},
			{"details", tr("Military: %1\nResources and surviving assets: %2\nTechnology and exploration: %3")
			                .arg(score.military).arg(score.economy).arg(score.technology)},
		});
	}
	if (rows != this->ranking) {
		this->ranking = std::move(rows);
		emit this->leaderboard_changed();
	}
	const auto snapshot = state->player_economy(player);
	if (snapshot != this->economy) {
		this->economy = snapshot;
		emit this->economy_changed();
	}
}

void HudQmlAdapter::refresh_mission() {
	const auto &runtime = this->sim_->get_trigger_runtime();
	if (not runtime) {
		// No objectives or victory state exist outside a scenario.
		return;
	}
	viewmodel::MissionView view{runtime->mission_state()};
	QString resources;
	QString route;
	for (const auto &objective : view.objectives()) {
		if (objective.id == "mission_01.march") {
			route = objective.state == "COMPLETED"
			        ? tr("Destroy the outpost, then lead the scout along the road to the village.")
			        : tr("Lead the scout to the first bend in the road.");
		}
		if (objective.id == "mission_02.stockpile") {
			const auto game = this->sim_->get_game();
			Is_True(game != nullptr and game->get_state() != nullptr,
			        ("HudQmlAdapter::refresh_mission: game or state is null"));
			const auto player = game->get_state()->get_player(0);
			Is_True(player != nullptr, ("HudQmlAdapter::refresh_mission: player 0 is null"));
			resources = tr("Food %1/50   Wood %2/50   Gold %3/50")
			                .arg(player->get_resource("de2_base.data.util.resource.types.Food"))
			                .arg(player->get_resource("de2_base.data.util.resource.types.Wood"))
			                .arg(player->get_resource("de2_base.data.util.resource.types.Gold"));
			break;
		}
	}
	const auto &instruction = runtime->mission_state().instruction();
	if (not instruction.empty()) {
		route = QString::fromStdString(instruction);
	}
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
