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
#include <cstdint>
#include <memory>

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
		NOTIFY selected_changed)

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
	QString mission_defeat_enemy_state() const { return this->ms_defeat_state_; }

signals:
	void selected_changed();
	void health_changed();
	void mission_changed();

private:
	void refresh_selection();
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
	QString ms_defeat_state_{"INACTIVE"};

	std::shared_ptr<openage::gamestate::GameSimulation> sim_;
	viewmodel::SelectionModel *model_{nullptr};
};


} // namespace openage::presenter