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
	Q_PROPERTY(QString actionStatus READ action_status NOTIFY action_status_changed)
	Q_PROPERTY(int constructionProgress READ construction_progress NOTIFY selection_details_changed)
	Q_PROPERTY(bool canStop READ can_stop NOTIFY selection_details_changed)
	Q_PROPERTY(int commandBarHeight READ command_bar_height CONSTANT)
	Q_PROPERTY(int economyBarWidth READ economy_bar_width CONSTANT)
	Q_PROPERTY(int economyBarHeight READ economy_bar_height CONSTANT)
	Q_PROPERTY(qint64 wood READ wood NOTIFY economy_changed)
	Q_PROPERTY(qint64 food READ food NOTIFY economy_changed)
	Q_PROPERTY(qint64 gold READ gold NOTIFY economy_changed)
	Q_PROPERTY(qint64 stone READ stone NOTIFY economy_changed)
	Q_PROPERTY(qint64 population READ population NOTIFY economy_changed)
	Q_PROPERTY(qint64 populationCapacity READ population_capacity NOTIFY economy_changed)
	Q_PROPERTY(bool mapUnits READ map_units NOTIFY map_options_changed)
	Q_PROPERTY(bool mapViewport READ map_viewport NOTIFY map_options_changed)
	Q_PROPERTY(QString minimap READ minimap NOTIFY map_changed)
	Q_PROPERTY(QVariantList diplomacy READ diplomacy NOTIFY diplomacy_changed)
	Q_PROPERTY(QString missionRoute READ mission_route NOTIFY mission_changed)
	Q_PROPERTY(QString missionStockpile READ mission_stockpile NOTIFY mission_changed)
	Q_PROPERTY(QString missionDefeatEnemyState READ mission_defeat_enemy_state
		NOTIFY mission_changed)

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
	QVariantList action_options() const { return this->actions; }
	QString action_status() const { return this->status; }
	void set_action_status(QString status);
	Q_INVOKABLE void requestAction(const QString &option, bool build) { emit action_requested(option, build); }
	int construction_progress() const { return this->construction; }
	bool can_stop() const { return this->stop_enabled; }
	int command_bar_height() const { return 146; }
	int economy_bar_width() const { return 360; }
	int economy_bar_height() const { return 42; }
	qint64 wood() const { return this->economy.wood; }
	qint64 food() const { return this->economy.food; }
	qint64 gold() const { return this->economy.gold; }
	qint64 stone() const { return this->economy.stone; }
	qint64 population() const { return this->economy.population; }
	qint64 population_capacity() const { return this->economy.population_capacity; }
	void refresh_economy(gamestate::player_id_t player);
	bool map_units() const { return this->show_units; }
	bool map_viewport() const { return this->show_viewport; }
	void set_selection_details(QVariantList cards, QVariantList actions, int construction, bool can_stop);
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

	std::shared_ptr<openage::gamestate::GameSimulation> sim_;
};


} // namespace openage::presenter
