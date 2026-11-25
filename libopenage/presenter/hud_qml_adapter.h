// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4A: HudQmlAdapter -- the QObject bridge between the Qt-free
// ViewModel layer (SelectedEntityView / MissionView /
// SelectionModel) and the QML HUD. The ViewModel never touches
// Qt; the adapter mirrors the model's snapshot into Q_PROPERTYs
// and emits change signals so the QML re-reads. This keeps the
// C++ viewmodel testable headlessly (demo 22/23) while the GUI
// binds to standard Qt properties.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QImage>
#include <cstdint>
#include <memory>
#include <optional>

#include "gamestate/types.h"
#include "presenter/viewmodel/selection_model.h"


namespace openage::gamestate {
class ChatConsole;
class GameSimulation;
}


namespace openage::presenter {


class HudQmlAdapter : public QObject {
	Q_OBJECT

	Q_PROPERTY(qint64 selectedEntityId READ selected_entity_id
		NOTIFY selected_changed)
	Q_PROPERTY(QString selectedName READ selected_name
		NOTIFY selected_changed)
	Q_PROPERTY(qint64 selectedOwnerPlayerId READ selected_owner_player_id
		NOTIFY selected_changed)
	Q_PROPERTY(bool selectedIsAlive READ selected_is_alive
		NOTIFY health_changed)

	Q_PROPERTY(qint64 selectedHpCurrent READ selected_hp_current
		NOTIFY health_changed)
	Q_PROPERTY(qint64 selectedHpMax READ selected_hp_max
		NOTIFY health_changed)

	Q_PROPERTY(qint64 selectedAttackDamage READ selected_attack_damage
		NOTIFY selected_changed)
	Q_PROPERTY(qreal selectedAttackRange READ selected_attack_range
		NOTIFY selected_changed)
	Q_PROPERTY(qreal selectedAttackCooldown READ selected_attack_cooldown
		NOTIFY selected_changed)
	Q_PROPERTY(bool selectedAttackIsActive READ selected_attack_is_active
		NOTIFY selected_changed)

	Q_PROPERTY(QString missionLifecycle READ mission_lifecycle
		NOTIFY mission_changed)
	Q_PROPERTY(bool missionIsVictory READ mission_is_victory
		NOTIFY mission_changed)
	Q_PROPERTY(QString missionVictoryReason READ mission_victory_reason
		NOTIFY mission_changed)
	Q_PROPERTY(QVariantList selectionCards READ selection_cards NOTIFY selection_details_changed)
	Q_PROPERTY(QVariantList actionOptions READ action_options NOTIFY selection_details_changed)
	Q_PROPERTY(QVariantMap commandIcons READ command_icons NOTIFY selection_details_changed)
	Q_PROPERTY(QString actionStatus READ action_status NOTIFY action_status_changed)
	Q_PROPERTY(int constructionProgress READ construction_progress NOTIFY selection_details_changed)
	Q_PROPERTY(bool canStop READ can_stop NOTIFY selection_details_changed)
	Q_PROPERTY(int commandBarHeight READ command_bar_height CONSTANT)
	Q_PROPERTY(int economyBarWidth READ economy_bar_width CONSTANT)
	Q_PROPERTY(int economyBarHeight READ economy_bar_height CONSTANT)
	Q_PROPERTY(QVariantList leaderboard READ leaderboard NOTIFY leaderboard_changed)
	Q_PROPERTY(qint64 wood READ wood NOTIFY economy_changed)
	Q_PROPERTY(qint64 food READ food NOTIFY economy_changed)
	Q_PROPERTY(qint64 gold READ gold NOTIFY economy_changed)
	Q_PROPERTY(qint64 stone READ stone NOTIFY economy_changed)
	Q_PROPERTY(qint64 population READ population NOTIFY economy_changed)
	Q_PROPERTY(qint64 populationCapacity READ population_capacity NOTIFY economy_changed)
	Q_PROPERTY(int idleVillagers READ idle_villagers NOTIFY idle_villagers_changed)
	Q_PROPERTY(bool mapUnits READ map_units NOTIFY map_options_changed)
	Q_PROPERTY(bool mapViewport READ map_viewport NOTIFY map_options_changed)
	Q_PROPERTY(QString minimap READ minimap NOTIFY map_changed)
	Q_PROPERTY(QVariantList diplomacy READ diplomacy NOTIFY diplomacy_changed)
	Q_PROPERTY(QString missionRoute READ mission_route NOTIFY mission_changed)
	Q_PROPERTY(QString missionStockpile READ mission_stockpile NOTIFY mission_changed)
	Q_PROPERTY(QString missionDefeatEnemyState READ mission_defeat_enemy_state
		NOTIFY mission_changed)
	Q_PROPERTY(bool chatOpen READ chat_open NOTIFY chat_open_changed)
	Q_PROPERTY(QVariantList chatMessages READ chat_messages NOTIFY chat_messages_changed)
	Q_PROPERTY(bool menuOpen READ menu_open NOTIFY menu_open_changed)
	Q_PROPERTY(QString menuPage READ menu_page NOTIFY menu_page_changed)
	Q_PROPERTY(QString menuStatus READ menu_status NOTIFY menu_status_changed)
	Q_PROPERTY(QVariantList saveSlots READ save_slots NOTIFY save_slots_changed)
	Q_PROPERTY(qreal gameSpeed READ game_speed NOTIFY settings_changed)
	Q_PROPERTY(bool fogEnabled READ fog_enabled NOTIFY settings_changed)

public:
	explicit HudQmlAdapter(QObject *parent = nullptr);
	~HudQmlAdapter() override;

	/**
	 * Subscribe to a SelectionModel and hold a reference to the
	 * simulation so the adapter can rebuild SelectedEntityView /
	 * MissionView snapshots on every event.
	 *
	 * Lifetime: the SelectionModel and GameSimulation must
	 * outlive this adapter (the adapter's event callbacks are
	 * stored in the model; the model fires them at any time).
	 * The presenter owns all three with the same lifetime.
	 */
	void attach(
		viewmodel::SelectionModel &model,
		openage::gamestate::GameSimulation &sim);

	qint64 selected_entity_id() const { return this->sel_id_; }
	QString selected_name() const { return this->sel_name_; }
	qint64 selected_owner_player_id() const { return this->sel_owner_; }
	bool selected_is_alive() const { return this->sel_alive_; }
	qint64 selected_hp_current() const { return this->sel_hp_cur_; }
	qint64 selected_hp_max() const { return this->sel_hp_max_; }
	qint64 selected_attack_damage() const { return this->sel_atk_dmg_; }
	qreal selected_attack_range() const { return this->sel_atk_rng_; }
	qreal selected_attack_cooldown() const { return this->sel_atk_cd_; }
	bool selected_attack_is_active() const { return this->sel_atk_active_; }
	QString mission_lifecycle() const { return this->ms_lifecycle_; }
	bool mission_is_victory() const { return this->ms_victory_; }
	QString mission_victory_reason() const { return this->ms_reason_; }
	QVariantList selection_cards() const { return this->cards; }
	QVariantMap command_icons() const { return this->command_images; }
	QVariantList action_options() const { return this->actions; }
	QString action_status() const { return this->status; }
	void set_action_status(QString status);
	Q_INVOKABLE void requestAction(const QString &option, bool build) { emit action_requested(option, build); }
	int construction_progress() const { return this->construction; }
	bool can_stop() const { return this->stop_enabled; }
	int command_bar_height() const { return 146; }
	int economy_bar_width() const { return 430; }
	int economy_bar_height() const { return 42; }
	QVariantList leaderboard() const { return this->ranking; }
	qint64 wood() const { return this->economy.wood; }
	qint64 food() const { return this->economy.food; }
	qint64 gold() const { return this->economy.gold; }
	qint64 stone() const { return this->economy.stone; }
	qint64 population() const { return this->economy.population; }
	qint64 population_capacity() const { return this->economy.population_capacity; }
	int idle_villagers() const { return this->idle_villagers_; }
	void set_idle_villagers(int count);
	Q_INVOKABLE void jumpIdleVillager() { emit idle_villager_requested(); }
	void refresh_economy(gamestate::player_id_t player);
	bool map_units() const { return this->show_units; }
	bool map_viewport() const { return this->show_viewport; }
	void set_selection_details(QVariantList cards, QVariantList actions, int construction, bool can_stop,
	                           QVariantMap command_icons);
	Q_INVOKABLE void selectCard(int index) { emit card_requested(index); }
	Q_INVOKABLE void stopSelected() { emit stop_requested(); }
	Q_INVOKABLE void clearSelection() { emit clear_requested(); }
	Q_INVOKABLE void toggleMapUnits();
	Q_INVOKABLE void toggleMapViewport();
	QString minimap() const { return this->minimap_url; }
	QVariantList diplomacy() const { return this->relations; }
	void set_minimap(QImage image);
	void set_diplomacy(QVariantList players);
	Q_INVOKABLE void moveCamera(qreal x, qreal y) { emit camera_requested(x, y); }
	Q_INVOKABLE void changeDiplomacy(int player, int stance) { emit diplomacy_requested(player, stance); }
	bool is_interactive(qreal x, qreal y, qreal width, qreal height) const;
	QString mission_route() const { return this->ms_route_; }
	QString mission_stockpile() const { return this->ms_stockpile_; }
	QString mission_defeat_enemy_state() const { return this->ms_defeat_state_; }
	bool chat_open() const { return this->chat_open_; }
	QVariantList chat_messages() const { return this->chat_messages_; }
	Q_INVOKABLE void openChat();
	Q_INVOKABLE void closeChat();
	Q_INVOKABLE void submitChat(const QString &text);
	Q_INVOKABLE void pasteToChat();
	bool menu_open() const { return this->menu_open_; }
	QString menu_page() const { return this->menu_page_; }
	QString menu_status() const { return this->menu_status_; }
	QVariantList save_slots() const { return this->save_slots_; }
	qreal game_speed() const { return this->game_speed_; }
	bool fog_enabled() const;
	void set_saves_dir(std::string dir) { this->saves_dir_ = std::move(dir); }
	Q_INVOKABLE void openMenu();
	Q_INVOKABLE void closeMenu();
	Q_INVOKABLE void toggleMenu();
	Q_INVOKABLE void setMenuPage(const QString &page);
	Q_INVOKABLE void refreshSaves();
	Q_INVOKABLE void saveGame(const QString &slotName);
	Q_INVOKABLE void loadGame(const QString &slotName);
	Q_INVOKABLE void setGameSpeed(qreal speed);
	Q_INVOKABLE void toggleFogSetting();
	Q_INVOKABLE void quitGame() { emit quit_requested(); }

signals:
	void selection_details_changed();
	void action_status_changed();
	void action_requested(const QString &option, bool build);
	void map_options_changed();
	void card_requested(int index);
	void stop_requested();
	void clear_requested();
	void map_changed();
	void diplomacy_changed();
	void camera_requested(qreal x, qreal y);
	void diplomacy_requested(int player, int stance);
	void selected_changed();
	void health_changed();
	void mission_changed();
	void economy_changed();
	void idle_villagers_changed();
	void idle_villager_requested();
	void leaderboard_changed();
	void chat_open_changed();
	void chat_messages_changed();
	void chat_paste_requested(const QString &text);
	void menu_open_changed();
	void menu_page_changed();
	void menu_status_changed();
	void save_slots_changed();
	void settings_changed();
	void game_loaded();
	void quit_requested();

private:
	void refresh_selection(std::optional<gamestate::entity_id_t> entity_id);
	void refresh_mission();

	qint64 sel_id_{-1};
	QString sel_name_;
	qint64 sel_owner_{-1};
	bool sel_alive_{false};
	qint64 sel_hp_cur_{0};
	qint64 sel_hp_max_{0};
	qint64 sel_atk_dmg_{0};
	qreal sel_atk_rng_{0.0};
	qreal sel_atk_cd_{0.0};
	bool sel_atk_active_{false};
	QString ms_lifecycle_{"RUNNING"};
	bool ms_victory_{false};
	QString ms_reason_;
	QString minimap_url;
	QImage minimap_image;
	QVariantList relations;
	QVariantList cards;
	QVariantMap command_images;
	QVariantList actions;
	QString status;
	int construction = -1;
	bool stop_enabled = false;
	bool show_units = true;
	bool show_viewport = true;
	QString ms_route_;
	QString ms_stockpile_;
	QString ms_defeat_state_{"INACTIVE"};
	gamestate::PlayerEconomy economy;
	int idle_villagers_{0};
	QVariantList ranking;
	bool chat_open_{false};
	QVariantList chat_messages_;
	bool menu_open_{false};
	QString menu_page_{"main"};
	QString menu_status_;
	QVariantList save_slots_;
	qreal game_speed_{1.0};
	qreal pre_menu_speed_{1.0};
	std::string saves_dir_{"saves"};

	std::shared_ptr<openage::gamestate::GameSimulation> sim_;
	std::unique_ptr<openage::gamestate::ChatConsole> chat_console_;
};


} // namespace openage::presenter
