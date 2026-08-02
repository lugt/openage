// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4A: MissionHud.qml -- AoE2-style bottom HUD skeleton.
//
// Layout is real (matches the AoE2 command panel: portrait, name,
// HP bar, attack/range/cooldown, current command, Move/Attack/Stop
// buttons; objective panel and victory panel). Visuals are
// non-proprietary placeholders (Rectangle, Text). Real art
// assets are P5 (targeted media closure).
//
// Data source: the QtObject `hud` is a placeholder that mirrors
// the Q_PROPERTYs on presenter::HudQmlAdapter. Production wiring
// assigns the C++ adapter to this QtObject (or replaces it with
// `OA.HudAdapter` once the OA module exposes it). The QML never
// reads simulation state directly -- every visible value is a
// property on `hud`, updated by the ViewModel event layer.

import QtQuick 2.4
import QtQuick.Controls 1.1
import QtQuick.Layouts 1.1

Item {
	id: root

	// === Data source placeholder ===
	// Real wiring: `hud = adapter` from C++ (presenter::HudQmlAdapter).
	// The default values let the QML render the layout even before
	// the adapter is bound, so the design is reviewable on its own.
	QtObject {
		id: hud
		property var selectedEntityId: -1
		property string selectedName: "PlayerVillager"
		property int selectedOwnerPlayerId: 0
		property bool selectedIsAlive: true
		property int selectedHpCurrent: 25
		property int selectedHpMax: 25
		property int selectedAttackDamage: 3
		property real selectedAttackRange: 1.0
		property real selectedAttackCooldown: 2.0
		property bool selectedAttackIsActive: false
		property string missionLifecycle: "RUNNING"
		property bool missionIsVictory: false
		property string missionVictoryReason: ""
		property string missionDefeatEnemyState: "INACTIVE"
	}

	readonly property int portraitSize: 96
	readonly property int bottomHeight: 200
	readonly property color hpColor: "#22cc22"
	readonly property color hpBackColor: "#332222"
	readonly property color panelColor: "#101820"
	readonly property color textColor: "#e0e0e0"
	readonly property color accentColor: "#d4a04a"

	width: 1280
	height: 720

	// === Objective panel (top-right) ===
	Rectangle {
		id: objectivePanel
		anchors.top: parent.top
		anchors.right: parent.right
		anchors.margins: 12
		width: 280
		height: 110
		color: panelColor
		border.color: accentColor
		radius: 4

		Column {
			anchors.fill: parent
			anchors.margins: 8
			spacing: 4

			Text {
				text: "Objectives"
				color: accentColor
				font.bold: true
			}
			Text {
				text: "Defeat the enemy: " + hud.missionDefeatEnemyState
				color: textColor
			}
		}
	}

	// === Victory panel (center, only when victorious) ===
	Rectangle {
		id: victoryPanel
		visible: hud.missionIsVictory
		anchors.centerIn: parent
		width: 360
		height: 140
		color: panelColor
		border.color: "#ffcc00"
		radius: 6

		Column {
			anchors.fill: parent
			anchors.margins: 12
			spacing: 6

			Text {
				text: "VICTORY"
				color: "#ffcc00"
				font.pixelSize: 28
				font.bold: true
				anchors.horizontalCenter: parent.horizontalCenter
			}
			Text {
				text: hud.missionVictoryReason.length > 0
					? ("Reason: " + hud.missionVictoryReason)
					: "Mission complete"
				color: textColor
				anchors.horizontalCenter: parent.horizontalCenter
			}
		}
	}

	// === Bottom command panel ===
	Rectangle {
		id: bottomPanel
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.bottom: parent.bottom
		height: bottomHeight
		color: panelColor
		border.color: accentColor

		Row {
			anchors.fill: parent
			anchors.margins: 8
			spacing: 12

			// Portrait area (placeholder rectangle).
			Rectangle {
				width: portraitSize
				height: portraitSize
				color: "#222"
				border.color: accentColor
				Text {
					anchors.centerIn: parent
					text: hud.selectedEntityId < 0 ? "?" : "V"
					color: textColor
				}
			}

			// Name / player / HP column.
			Column {
				width: 280
				spacing: 6

				Text {
					text: hud.selectedEntityId < 0
						? "(no selection)"
						: hud.selectedName + "  (P" + hud.selectedOwnerPlayerId + ")"
					color: textColor
					font.bold: true
				}

				// HP bar (background + foreground).
				Rectangle {
					width: parent.width
					height: 18
					color: hpBackColor
					Rectangle {
						width: hud.selectedHpMax > 0
							? parent.width * (hud.selectedHpCurrent / hud.selectedHpMax)
							: 0
						height: parent.height
						color: hud.selectedIsAlive ? hpColor : "#882222"
					}
					Text {
						anchors.centerIn: parent
						text: hud.selectedHpCurrent + " / " + hud.selectedHpMax
						color: "#000"
					}
				}

				Text {
					text: "Atk " + hud.selectedAttackDamage
						+ "  Rng " + hud.selectedAttackRange.toFixed(1)
						+ "  CD " + hud.selectedAttackCooldown.toFixed(1) + "s"
					color: textColor
				}
				Text {
					text: hud.selectedAttackIsActive
						? "State: Attacking"
						: "State: Idle"
					color: textColor
				}
			}

			// Command buttons (Move / Attack / Stop).
			// Each button calls into the Commander via a placeholder
			// function `OA.issueCommand(name)`. Production wiring
			// exposes a QML-callable bridge that funnels every
			// command through gamestate::event::Commander --
			// the GUI never invokes AttackHandler directly.
			Row {
				spacing: 6
				Repeater {
					model: ["Move", "Attack", "Stop"]
					delegate: ButtonFlat {
						text: modelData
						// Real handler: a Commander bridge method
						// (added in P4.4D). For the skeleton the
						// button is clickable but no gameplay effect
						// is dispatched; P4.4D wires the bridge.
					}
				}
			}
		}
	}
}
