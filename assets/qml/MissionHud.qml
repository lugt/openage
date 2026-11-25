// Copyright 2026 the openage authors. See copying.md for legal info.
//
import QtQuick

Item {
	id: root

	readonly property QtObject hud: missionHud
	readonly property int bottomHeight: hud.commandBarHeight
	readonly property color panelColor: "#101820"
	readonly property color textColor: "#e0e0e0"
	readonly property color accentColor: "#d4a04a"

	width: 1280
	height: 720

	Rectangle {
		id: economyBar
		objectName: "economyBar"
		anchors.top: parent.top
		anchors.left: parent.left
		anchors.margins: 12
		width: Math.max(0, Math.min(hud.economyBarWidth, root.width - 24))
		height: hud.economyBarHeight
		color: panelColor
		border.color: accentColor
		radius: 4

		Row {
			anchors.fill: parent
			anchors.margins: 1
			Repeater {
				model: [
					{ label: qsTr("Wood"), value: hud.wood, badge: "#8c5a32", warn: false, clickable: false },
					{ label: qsTr("Food"), value: hud.food, badge: "#b83b3b", warn: false, clickable: false },
					{ label: qsTr("Gold"), value: hud.gold, badge: "#d4a024", warn: false, clickable: false },
					{ label: qsTr("Stone"), value: hud.stone, badge: "#8d96a0", warn: false, clickable: false },
					{ label: qsTr("Pop"), value: qsTr("%1/%2").arg(hud.population).arg(hud.populationCapacity), badge: "#4d7ea8", warn: hud.populationCapacity > 0 && hud.population >= hud.populationCapacity, clickable: false },
					{ label: qsTr("Idle"), value: hud.idleVillagers, badge: "#d99741", warn: hud.idleVillagers > 0, clickable: true }
				]
				delegate: Rectangle {
					id: counter
					required property var modelData
					required property int index
					width: (economyBar.width - 2) / 6
					height: economyBar.height - 2
					color: counter.modelData.clickable && counterMouse.containsMouse ? "#24303c"
					       : (counter.modelData.warn ? "#2d2015" : "transparent")

					Rectangle {
						anchors.right: parent.right
						anchors.verticalCenter: parent.verticalCenter
						width: 1
						height: parent.height - 12
						color: "#514535"
						visible: counter.index < 5
					}
					Column {
						width: Math.max(0, parent.width - 8)
						anchors.centerIn: parent
						spacing: 2
						Row {
							anchors.horizontalCenter: parent.horizontalCenter
							spacing: 4
							Rectangle {
								width: 7
								height: 7
								radius: 2
								anchors.verticalCenter: parent.verticalCenter
								color: counter.modelData.badge
							}
							Text {
								text: counter.modelData.label
								font.pixelSize: 11
								elide: Text.ElideRight
								color: counter.modelData.warn ? "#ffd479" : "#a7b7c5"
							}
						}
						Text {
							width: parent.width
							text: counter.modelData.value
							horizontalAlignment: Text.AlignHCenter
							font.pixelSize: 14
							font.bold: true
							fontSizeMode: Text.HorizontalFit
							minimumPixelSize: 9
							color: counter.modelData.warn ? "#ffcf66" : textColor
						}
					}
					MouseArea {
						id: counterMouse
						anchors.fill: parent
						enabled: counter.modelData.clickable
						hoverEnabled: counter.modelData.clickable
						cursorShape: counter.modelData.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
						onClicked: hud.jumpIdleVillager()
					}
				}
			}
		}
	}

	Rectangle {
		id: leaderboard
		objectName: "leaderboard"
		property bool collapsed: false
		anchors.right: parent.right
		anchors.bottom: parent.bottom
		anchors.rightMargin: 12
		anchors.bottomMargin: root.bottomHeight + 12
		width: Math.min(240, root.width - 24)
		height: collapsed ? 30 : 32 + rankingRows.height
		visible: hud.leaderboard.length > 0
		color: "#d9101820"
		border.color: accentColor
		radius: 4
		Text {
			x: 10; y: 7
			text: qsTr("Scores")
			color: accentColor
			font.bold: true
			font.pixelSize: 13
		}
		Rectangle {
			anchors.right: parent.right
			anchors.top: parent.top
			anchors.margins: 5
			width: 20
			height: 20
			radius: 3
			color: leaderboardToggle.containsMouse ? "#2c343d" : "#1a222b"
			border.color: "#514535"
			Text {
				anchors.centerIn: parent
				text: leaderboard.collapsed ? "+" : "−"
				color: accentColor
				font.bold: true
				font.pixelSize: 12
			}
			MouseArea {
				id: leaderboardToggle
				anchors.fill: parent
				hoverEnabled: true
				onClicked: leaderboard.collapsed = !leaderboard.collapsed
			}
		}
		Column {
			id: rankingRows
			x: 6; y: 28
			width: parent.width - 12
			visible: !leaderboard.collapsed
			Repeater {
				model: hud.leaderboard
				delegate: Rectangle {
					id: scoreRow
					required property var modelData
					required property int index
					readonly property var playerColors: ["#549aff", "#ff6262", "#b5b5b5", "#62dc68", "#ffdf55", "#55e6e6", "#e28aff", "#ffab55"]
					width: rankingRows.width
					height: 26
					color: index % 2 === 0 ? "#303e4a" : "transparent"
					Text {
						x: 5
						anchors.verticalCenter: parent.verticalCenter
						width: parent.width - scoreValue.width - 20
						text: (scoreRow.index + 1) + ". " + scoreRow.modelData.name
						elide: Text.ElideRight
						color: scoreRow.playerColors[scoreRow.modelData.player % scoreRow.playerColors.length]
						font.pixelSize: 13
					}
					Text {
						id: scoreValue
						anchors.right: parent.right
						anchors.rightMargin: 5
						anchors.verticalCenter: parent.verticalCenter
						text: scoreRow.modelData.score
						color: textColor
						font.bold: true
						font.pixelSize: 13
					}
					MouseArea {
						id: scoreHover
						anchors.fill: parent
						hoverEnabled: true
					}
					Rectangle {
						anchors.right: parent.left
						anchors.rightMargin: 8
						width: 280
						height: breakdown.implicitHeight + 20
						visible: scoreHover.containsMouse
						color: panelColor
						border.color: accentColor
						z: 10
						Text {
							id: breakdown
							anchors.fill: parent
							anchors.margins: 10
							text: scoreRow.modelData.details
							wrapMode: Text.WordWrap
							color: textColor
							font.pixelSize: 13
						}
					}
				}
			}
		}
	}

	// === Top Menu Button (next to economy bar) ===
	Rectangle {
		id: menuButton
		anchors.top: parent.top
		anchors.left: economyBar.right
		anchors.topMargin: 12
		anchors.leftMargin: 8
		width: 96
		height: 32
		visible: root.width >= hud.economyBarWidth + 420
		color: menuBtnMouse.containsMouse ? "#2c343d" : panelColor
		border.color: accentColor
		radius: 4
		Text {
			anchors.centerIn: parent
			text: qsTr("Menu (Esc)")
			color: textColor
			font.pixelSize: 12
			font.bold: true
		}
		MouseArea {
			id: menuBtnMouse
			anchors.fill: parent
			hoverEnabled: true
			onClicked: hud.toggleMenu()
		}
	}

	// === Objective panel (top-right) ===
	Rectangle {
		id: objectivePanel
		anchors.top: parent.top
		anchors.right: parent.right
		anchors.margins: 12
		anchors.topMargin: root.width < hud.economyBarWidth + 316 ? economyBar.y + economyBar.height + 8 : 12
		width: Math.max(0, Math.min(280, root.width - 24))
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
				text: hud.missionStockpile.length > 0 ? hud.missionStockpile
				      : hud.missionDefeatEnemyState === "COMPLETED"
				      ? qsTr("Enemy defeated") : qsTr("Defeat the enemy forces")
				color: textColor
			}
			Text {
				text: hud.missionStockpile.length > 0
				      ? qsTr("Select a villager, then right-click a bush, tree, or gold deposit.")
				      : hud.missionRoute.length > 0 ? hud.missionRoute
				      : qsTr("Left-click to select. Right-click to move or attack.")
				width: parent.width
				wrapMode: Text.WordWrap
				visible: !hud.missionIsVictory
				color: textColor
			}
		}
	}

	Rectangle {
		anchors.left: parent.left
		anchors.top: parent.top
		anchors.margins: 12
		anchors.topMargin: root.width < 534 ? objectivePanel.y + objectivePanel.height + 8 : economyBar.y + economyBar.height + 8
		width: Math.max(0, Math.min(230, root.width - 24))
		height: 32 + 28 * hud.diplomacy.length
		color: panelColor
		border.color: accentColor
		radius: 4
		visible: hud.diplomacy.length > 0
		Column {
			anchors.fill: parent
			anchors.margins: 8
			Text {
				height: 20
				text: qsTr("Diplomacy")
				color: accentColor
				font.bold: true
			}
			Repeater {
				model: hud.diplomacy
				delegate: Row {
					id: relationRow
					required property var modelData
					height: 28
					spacing: 3
					Text {
						width: 62
						height: 22
						text: modelData.name
						elide: Text.ElideRight
						color: textColor
						verticalAlignment: Text.AlignVCenter
					}
					Repeater {
						model: [qsTr("Ally"), qsTr("Neutral"), qsTr("Enemy")]
						delegate: Rectangle {
							required property string modelData
							required property int index
							width: 47
							height: 22
							radius: 3
							color: relationRow.modelData.stance === index + 1 ? "#81602d" : "#242d37"
							Text {
								anchors.centerIn: parent
								text: parent.modelData
								color: textColor
								font.pixelSize: 11
							}
							MouseArea {
								anchors.fill: parent
								onClicked: hud.changeDiplomacy(relationRow.modelData.id, parent.index + 1)
							}
						}
					}
				}
			}
		}
	}

	// === Victory panel (center, only when victorious) ===
	Rectangle {
		id: victoryPanel
		objectName: "missionVictoryPanel"
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
				text: qsTr("Mission complete")
				color: textColor
				anchors.horizontalCenter: parent.horizontalCenter
			}
		}
	}

	component IconCommandButton: Rectangle {
		id: iconButton
		required property string tooltip
		property string fallbackText: ""
		property string hotkey: ""
		property bool active: false
		property url iconSource: ""
		signal pressed
		signal hoveredChanged(bool hovered)
		width: 40
		height: 40
		radius: 3
		Accessible.name: tooltip
		border.color: iconControl.containsMouse ? (enabled ? "#d6b677" : "#8b4a4a")
		              : (enabled && active ? "#d6b677" : (enabled ? "#5c5040" : "#443636"))
		color: enabled ? (active || iconControl.pressed ? "#645037" : "#2b2a29") : "#1e1b1b"
		Image {
			id: iconImage
			anchors.fill: parent
			anchors.margins: 4
			source: iconButton.iconSource
			fillMode: Image.PreserveAspectFit
			opacity: iconButton.enabled ? 1.0 : 0.35
		}
		Text {
			anchors.centerIn: parent
			width: parent.width - 6
			visible: iconImage.status !== Image.Ready
			horizontalAlignment: Text.AlignHCenter
			elide: Text.ElideRight
			text: iconButton.fallbackText
			color: iconButton.enabled ? "#f7e6c8" : "#7b756c"
			font.pixelSize: 10
			font.bold: true
		}
		Rectangle {
			visible: iconButton.hotkey.length > 0
			anchors.right: parent.right
			anchors.bottom: parent.bottom
			anchors.margins: 2
			width: hotkeyLabel.implicitWidth + 4
			height: 12
			radius: 2
			color: "#cc101418"
			Text {
				id: hotkeyLabel
				anchors.centerIn: parent
				text: iconButton.hotkey
				color: iconButton.enabled ? "#f3d89b" : "#7b756c"
				font.pixelSize: 9
				font.bold: true
			}
		}
		MouseArea {
			id: iconControl
			anchors.fill: parent
			hoverEnabled: true
			onContainsMouseChanged: iconButton.hoveredChanged(containsMouse)
			onClicked: {
				if (iconButton.enabled) {
					iconButton.pressed()
				}
			}
		}
		Component.onDestruction: iconButton.hoveredChanged(false)
	}

	component CommandButton: Rectangle {
		id: button
		required property string label
		property bool active: false
		property url iconSource: ""
		signal pressed
		width: 86
		height: 34
		radius: 3
		border.color: enabled && (active || control.containsMouse) ? "#d6b677" : "#5c5040"
		color: enabled ? (active || control.pressed ? "#645037" : "#2b2a29") : "#222020"
		Image {
			id: actionIcon
			anchors.left: parent.left
			anchors.leftMargin: 3
			anchors.verticalCenter: parent.verticalCenter
			width: 28
			height: 28
			source: button.iconSource
			fillMode: Image.PreserveAspectFit
		}
		Text {
			anchors.left: parent.left
			anchors.leftMargin: actionIcon.status === Image.Ready ? 34 : 4
			anchors.right: parent.right
			anchors.rightMargin: 4
			anchors.verticalCenter: parent.verticalCenter
			horizontalAlignment: Text.AlignHCenter
			elide: Text.ElideRight
			text: button.label
			color: button.enabled ? "#f7e6c8" : "#7b756c"
			font.pixelSize: 12
			font.bold: true
		}
		MouseArea {
			id: control
			anchors.fill: parent
			enabled: button.enabled
			hoverEnabled: true
			onClicked: {
				if (button.enabled) {
					button.pressed()
				}
			}
		}
	}

	Rectangle {
		id: bottomPanel
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.bottom: parent.bottom
		height: bottomHeight
		color: "#24221f"
		border.width: 2
		border.color: "#807258"

		Rectangle {
			id: commands
			property string hoveredTooltip: ""
			Connections {
				target: hud
				function onSelection_details_changed() {
					commands.hoveredTooltip = ""
				}
			}
			anchors.left: parent.left
			anchors.top: parent.top
			anchors.bottom: parent.bottom
			anchors.margins: 4
			width: Math.min(226, root.width * 0.28)
			color: "#191918"
			border.color: "#625441"
			Flickable {
				id: commandFlickable
				anchors.fill: parent
				anchors.margins: 5
				contentHeight: commandGrid.implicitHeight
				contentWidth: width
				interactive: contentHeight > height
				clip: true
				boundsBehavior: Flickable.StopAtBounds
				Grid {
					id: commandGrid
					readonly property int cellSpacing: 4
					readonly property int cellSize: Math.max(24, Math.min(40, Math.floor((width - cellSpacing * 4) / 5)))
					width: commandFlickable.width
					columns: 5
					spacing: cellSpacing
					Repeater {
						model: hud.actionOptions
						delegate: IconCommandButton {
							required property var modelData
							required property int index
							readonly property var gridKeys: ["Q", "W", "E", "R", "T", "A", "S", "D", "F", "G", "Z", "X", "C", "V", "B"]
							readonly property string keyLabel: index < gridKeys.length ? gridKeys[index] : ""
							readonly property string actionTitle: modelData.transform ? modelData.name
								: (modelData.build ? qsTr("Build %1") : modelData.research ? qsTr("Research %1") : qsTr("Train %1")).arg(modelData.name)
							readonly property string richTooltip: {
								var html = "<b style='color:#f3d89b'>" + actionTitle + "</b>"
								if (keyLabel.length > 0) {
									html += " <span style='color:#a7b7c5'>[" + keyLabel + "]</span>"
								}
								if (modelData.costs && modelData.costs.length > 0) {
									var parts = []
									for (var i = 0; i < modelData.costs.length; ++i) {
										var c = modelData.costs[i]
										var color = c.enough ? "#a3e08b" : "#ff6b6b"
										parts.push("<span style='color:" + color + "'>" + c.amount + " " + c.label + "</span>")
									}
									html += "<br/>" + parts.join(" · ")
								}
								if (modelData.available === false) {
									html += "<br/><span style='color:#ff6b6b'>" + qsTr("Requires prerequisite building or age") + "</span>"
								}
								return html
							}
							width: commandGrid.cellSize
							height: commandGrid.cellSize
							enabled: modelData.enabled !== false
							hotkey: keyLabel
							iconSource: modelData.icon || ""
							fallbackText: modelData.name ? modelData.name.slice(0, 2) : ""
							tooltip: richTooltip
							onHoveredChanged: (hovered) => {
								if (hovered) {
									commands.hoveredTooltip = tooltip
								}
								else if (commands.hoveredTooltip === tooltip) {
									commands.hoveredTooltip = ""
								}
							}
							onPressed: hud.requestAction(modelData.id, modelData.build)
						}
					}
					IconCommandButton {
						width: commandGrid.cellSize
						height: commandGrid.cellSize
						hotkey: "S"
						iconSource: hud.commandIcons.stop || ""
						fallbackText: qsTr("Stop")
						tooltip: "<b style='color:#f3d89b'>" + qsTr("Stop") + "</b> <span style='color:#a7b7c5'>[S]</span>"
						enabled: hud.canStop
						onHoveredChanged: (hovered) => {
							if (hovered) {
								commands.hoveredTooltip = tooltip
							}
							else if (commands.hoveredTooltip === tooltip) {
								commands.hoveredTooltip = ""
							}
						}
						onPressed: hud.stopSelected()
					}
					IconCommandButton {
						width: commandGrid.cellSize
						height: commandGrid.cellSize
						hotkey: "Esc"
						iconSource: hud.commandIcons.cancel || ""
						fallbackText: qsTr("Esc")
						tooltip: "<b style='color:#f3d89b'>" + qsTr("Deselect") + "</b>"
						enabled: hud.selectionCards.length > 0
						onHoveredChanged: (hovered) => {
							if (hovered) {
								commands.hoveredTooltip = tooltip
							}
							else if (commands.hoveredTooltip === tooltip) {
								commands.hoveredTooltip = ""
							}
						}
						onPressed: hud.clearSelection()
					}
				}
			}
		}

		Rectangle {
			id: mapPanel
			anchors.right: parent.right
			anchors.top: parent.top
			anchors.bottom: parent.bottom
			anchors.margins: 4
			width: Math.min(210, root.width * 0.30)
			color: "#191918"
			border.color: "#625441"

			Rectangle {
				id: mapFrame
				anchors.left: parent.left
				anchors.verticalCenter: parent.verticalCenter
				anchors.leftMargin: 6
				width: Math.min(126, parent.width - 65)
				height: width
				color: "#0d141b"
				border.color: "#aa9066"
				Minimap {
					anchors.fill: parent
					anchors.margins: 2
					source: hud.minimap
					onActivated: (x, y) => hud.moveCamera(x, y)
				}
			}
			Column {
				anchors.left: mapFrame.right
				anchors.right: parent.right
				anchors.margins: 5
				anchors.verticalCenter: parent.verticalCenter
				spacing: 5
				CommandButton {
					width: parent.width
					height: 28
					label: qsTr("Units")
					active: hud.mapUnits
					enabled: hud.minimap.length > 0
					onPressed: hud.toggleMapUnits()
				}
				CommandButton {
					width: parent.width
					height: 28
					label: qsTr("View")
					active: hud.mapViewport
					enabled: hud.minimap.length > 0
					onPressed: hud.toggleMapViewport()
				}
			}
		}

		Rectangle {
			anchors.left: commands.right
			anchors.right: mapPanel.left
			anchors.top: parent.top
			anchors.bottom: parent.bottom
			anchors.margins: 4
			border.color: "#9b7951"
			gradient: Gradient {
				GradientStop { position: 0; color: "#efd8ab" }
				GradientStop { position: 1; color: "#c9a476" }
			}

			Flickable {
				id: selectionPanel
				anchors.fill: parent
				anchors.margins: 9
				contentWidth: width
				contentHeight: details.implicitHeight
				interactive: contentHeight > height
				clip: true
				boundsBehavior: Flickable.StopAtBounds

				Column {
					id: details
					width: parent.width
					spacing: 5

					Text {
						readonly property var card: hud.selectionCards.length ? hud.selectionCards[0] : null
						text: !card ? qsTr("No selection")
						      : hud.selectionCards.length > 1
						      ? qsTr("%1 selected").arg(hud.selectionCards.length) + "  ·  " + card.owner
						      : card.name + "  ·  " + card.owner
						color: "#37281c"
						font.bold: true
						font.pixelSize: 16
					}

					Text {
						visible: hud.selectionCards.length > 0
						readonly property var card: visible ? hud.selectionCards[0] : null
						text: !card ? ""
						      : qsTr("Player: %1  |  Civ: %2").arg(card.owner).arg(card.civ || "Unknown")
						color: "#5b4031"
						font.pixelSize: 11
						font.bold: true
					}

					Text {
						visible: hud.selectionCards.length === 1
						readonly property var card: visible ? hud.selectionCards[0] : null
						text: card && card.maxHp > 0 ? qsTr("HP %1/%2").arg(card.hp).arg(card.maxHp)
						      + (hud.selectedAttackDamage > 0 ? qsTr("    Attack %1    Range %2")
						         .arg(hud.selectedAttackDamage).arg(hud.selectedAttackRange.toFixed(1)) : "")
						      : ""
						color: "#463326"
						font.pixelSize: 12
					}

					Flow {
						width: parent.width
						spacing: 5
						Repeater {
							model: hud.selectionCards
							delegate: Rectangle {
								id: card
								required property var modelData
								required property int index
								width: 39
								height: 39
								color: "#292723"
								border.color: cardClick.containsMouse ? "#fff5d9" : "#8a6e49"
								Accessible.name: modelData.name
								Image {
									id: portrait
									anchors.fill: parent
									anchors.margins: 2
									source: card.modelData.icon || ""
									fillMode: Image.PreserveAspectFit
								}
								Text {
									anchors.centerIn: parent
									visible: portrait.status !== Image.Ready
									text: card.modelData.name.split(" ").map(word => word.charAt(0)).join("").slice(0, 3)
									color: "#f2e4c6"
									font.bold: true
									font.pixelSize: 15
								}
								Rectangle {
									anchors.left: parent.left
									anchors.bottom: parent.bottom
									anchors.margins: 2
									height: 4
									readonly property real hpRatio: card.modelData.maxHp > 0
									       ? Math.max(0, Math.min(1, card.modelData.hp / card.modelData.maxHp)) : 1
									width: (parent.width - 4) * hpRatio
									color: hpRatio > 0.5 ? "#57d157" : (hpRatio > 0.25 ? "#e6c84a" : "#e64a4a")
								}
								MouseArea {
									id: cardClick
									anchors.fill: parent
									hoverEnabled: true
									onClicked: hud.selectCard(card.index)
								}
							}
						}
					}

					Text {
						visible: hud.actionStatus.length > 0
						width: parent.width
						text: hud.actionStatus
						color: "#493422"
						wrapMode: Text.WordWrap
						font.pixelSize: 12
					}

					Row {
						visible: hud.constructionProgress >= 0
						spacing: 8
						Text {
							text: qsTr("Construction %1%").arg(hud.constructionProgress)
							color: "#493422"
							font.bold: true
							font.pixelSize: 12
						}
						Rectangle {
							visible: hud.constructionProgress >= 0
							width: 90
							height: 9
							anchors.verticalCenter: parent.verticalCenter
							color: "#5b4031"
							Rectangle {
								width: parent.width * Math.max(0, hud.constructionProgress) / 100
								height: parent.height
								color: "#d99741"
							}
						}
					}
				}
			}
		}
	}

	Rectangle {
		id: commandTooltip
		anchors.left: bottomPanel.left
		anchors.leftMargin: 4
		anchors.bottom: bottomPanel.top
		anchors.bottomMargin: 6
		visible: commands.hoveredTooltip.length > 0
		width: Math.min(360, commandTooltipText.implicitWidth + 16)
		height: commandTooltipText.implicitHeight + 10
		color: "#e6101820"
		border.color: accentColor
		radius: 4
		z: 20
		Text {
			id: commandTooltipText
			anchors.centerIn: parent
			width: Math.min(344, implicitWidth)
			text: commands.hoveredTooltip
			textFormat: Text.RichText
			color: "#f7e6c8"
			font.pixelSize: 12
			wrapMode: Text.WordWrap
		}
	}

	// === In-game Chat / Cheat / Debug / Python Console (Enter to open) ===
	Column {
		id: chatOverlay
		anchors.left: parent.left
		anchors.bottom: parent.bottom
		anchors.leftMargin: 12
		anchors.bottomMargin: root.bottomHeight + 12
		width: Math.max(0, Math.min(520, root.width - 24))
		spacing: 6
		visible: hud.chatOpen || hud.chatMessages.length > 0

		Rectangle {
			width: parent.width
			height: Math.min(170, chatListColumn.implicitHeight + 12)
			visible: hud.chatMessages.length > 0
			color: hud.chatOpen ? "#cc101820" : "#88101820"
			border.color: hud.chatOpen ? accentColor : "#554433"
			radius: 4

			Column {
				id: chatListColumn
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.top: parent.top
				anchors.margins: 6
				spacing: 2

				Repeater {
					model: hud.chatMessages
					delegate: Text {
						required property var modelData
						width: chatListColumn.width
						text: modelData.sender + ": " + modelData.text
						color: modelData.color
						font.pixelSize: 12
						wrapMode: Text.WordWrap
						maximumLineCount: 2
						elide: Text.ElideRight
					}
				}
			}
		}

		Rectangle {
			id: chatInputBox
			width: parent.width
			height: 32
			visible: hud.chatOpen
			color: "#e6101820"
			border.color: accentColor
			border.width: 1
			radius: 4

			MouseArea {
				anchors.fill: parent
				onPressed: {
					hud.openChat()
					chatInput.forceActiveFocus()
				}
			}

			Text {
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.verticalCenter: parent.verticalCenter
				anchors.leftMargin: 8
				anchors.rightMargin: 8
				text: qsTr("Chat, AoE2 cheat, /help, or py <code>... (Enter to send, Esc to close)")
				color: "#7f8c9a"
				font.pixelSize: 12
				elide: Text.ElideRight
				visible: chatInput.text.length === 0
			}

			TextInput {
				id: chatInput
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.verticalCenter: parent.verticalCenter
				anchors.leftMargin: 8
				anchors.rightMargin: 8
				color: "#f8fafc"
				font.pixelSize: 13
				clip: true
				focus: hud.chatOpen
				activeFocusOnPress: true
				selectByMouse: true
				onVisibleChanged: {
					if (visible) {
						Qt.callLater(chatInput.forceActiveFocus)
					}
				}
				onActiveFocusChanged: {
					if (hud.chatOpen && !activeFocus) {
						Qt.callLater(chatInput.forceActiveFocus)
					}
				}
				onAccepted: {
					var msg = chatInput.text
					chatInput.text = ""
					hud.submitChat(msg)
				}
				Keys.onEscapePressed: {
					chatInput.text = ""
					hud.closeChat()
				}
			}

			Connections {
				target: hud
				function onChat_open_changed() {
					if (hud.chatOpen) {
						Qt.callLater(chatInput.forceActiveFocus)
					}
				}
				function onChat_paste_requested(text) {
					chatInput.insert(chatInput.cursorPosition, text)
				}
			}
		}
	}

	// === ESC In-Game Pause Menu (Resume / Save & Load / Settings / Quit) ===
	Rectangle {
		id: gameMenuBackdrop
		anchors.fill: parent
		color: "#99000000"
		visible: hud.menuOpen
		z: 100

		MouseArea {
			anchors.fill: parent
		}

		Rectangle {
			id: gameMenuPanel
			anchors.centerIn: parent
			width: Math.min(440, root.width - 32)
			height: Math.min(440, root.height - 32)
			color: "#141b22"
			border.color: accentColor
			border.width: 2
			radius: 6

			Column {
				anchors.fill: parent
				anchors.margins: 18
				spacing: 12

				Text {
					width: parent.width
					horizontalAlignment: Text.AlignHCenter
					text: hud.menuPage === "saveload" ? qsTr("Save / Load Game")
					      : hud.menuPage === "settings" ? qsTr("Game Settings")
					      : qsTr("Game Menu (Paused)")
					color: accentColor
					font.pixelSize: 20
					font.bold: true
				}

				Text {
					width: parent.width
					horizontalAlignment: Text.AlignHCenter
					visible: hud.menuStatus.length > 0
					text: hud.menuStatus
					color: "#a7f3d0"
					font.pixelSize: 12
					elide: Text.ElideRight
				}

				// --- Main Menu Page ---
				Column {
					width: parent.width
					spacing: 10
					visible: hud.menuPage === "main"

					CommandButton {
						width: parent.width
						height: 40
						label: qsTr("Resume Game (Esc)")
						onPressed: hud.closeMenu()
					}
					CommandButton {
						width: parent.width
						height: 40
						label: qsTr("Save / Load Game")
						onPressed: hud.setMenuPage("saveload")
					}
					CommandButton {
						width: parent.width
						height: 40
						label: qsTr("Settings")
						onPressed: hud.setMenuPage("settings")
					}
					CommandButton {
						width: parent.width
						height: 40
						label: qsTr("Quit Game")
						onPressed: hud.quitGame()
					}
				}

				// --- Save / Load Page ---
				Column {
					width: parent.width
					spacing: 8
					visible: hud.menuPage === "saveload"

					Row {
						width: parent.width
						spacing: 8

						Rectangle {
							width: parent.width - 96
							height: 34
							color: "#0d141b"
							border.color: "#6b5b45"
							radius: 3

							TextInput {
								id: saveSlotInput
								anchors.fill: parent
								anchors.margins: 8
								verticalAlignment: TextInput.AlignVCenter
								color: "#f8fafc"
								font.pixelSize: 13
								text: "quicksave"
								selectByMouse: true
							}
						}

						CommandButton {
							width: 88
							height: 34
							label: qsTr("Save")
							onPressed: hud.saveGame(saveSlotInput.text)
						}
					}

					Text {
						text: qsTr("Saved Games:")
						color: "#a7b7c5"
						font.pixelSize: 12
					}

					Flickable {
						width: parent.width
						height: 170
						contentWidth: width
						contentHeight: savesColumn.implicitHeight
						clip: true
						interactive: contentHeight > height

						Column {
							id: savesColumn
							width: parent.width
							spacing: 6

							Text {
								visible: hud.saveSlots.length === 0
								text: qsTr("No saved games found yet.")
								color: "#7f8c9a"
								font.pixelSize: 12
							}

							Repeater {
								model: hud.saveSlots
								delegate: Row {
									required property var modelData
									width: savesColumn.width
									spacing: 8

									Rectangle {
										width: parent.width - 86
										height: 32
										color: "#1b242d"
										border.color: "#43505c"
										radius: 3
										Text {
											anchors.fill: parent
											anchors.margins: 6
											verticalAlignment: Text.AlignVCenter
											text: modelData.name + (modelData.summary ? " (" + modelData.summary + ")" : "")
											color: textColor
											font.pixelSize: 12
											elide: Text.ElideRight
										}
									}
									CommandButton {
										width: 78
										height: 32
										label: qsTr("Load")
										onPressed: hud.loadGame(modelData.name)
									}
								}
							}
						}
					}

					CommandButton {
						width: parent.width
						height: 36
						label: qsTr("Back")
						onPressed: hud.setMenuPage("main")
					}
				}

				// --- Settings Page ---
				Column {
					width: parent.width
					spacing: 10
					visible: hud.menuPage === "settings"

					Text {
						text: qsTr("Game Speed: %1x").arg(hud.gameSpeed.toFixed(1))
						color: textColor
						font.pixelSize: 13
					}
					Row {
						width: parent.width
						spacing: 6
						Repeater {
							model: [0.5, 1.0, 1.5, 2.0, 3.0]
							delegate: CommandButton {
								required property real modelData
								width: (parent.width - 24) / 5
								height: 32
								active: Math.abs(hud.gameSpeed - modelData) < 0.05
								label: modelData.toFixed(1) + "x"
								onPressed: hud.setGameSpeed(modelData)
							}
						}
					}
					CommandButton {
						width: parent.width
						height: 36
						active: hud.fogEnabled
						label: qsTr("Fog of War: %1").arg(hud.fogEnabled ? qsTr("ON") : qsTr("OFF"))
						onPressed: hud.toggleFogSetting()
					}
					CommandButton {
						width: parent.width
						height: 36
						active: hud.mapUnits
						label: qsTr("Minimap Units: %1").arg(hud.mapUnits ? qsTr("ON") : qsTr("OFF"))
						onPressed: hud.toggleMapUnits()
					}
					CommandButton {
						width: parent.width
						height: 36
						active: hud.mapViewport
						label: qsTr("Minimap Viewport: %1").arg(hud.mapViewport ? qsTr("ON") : qsTr("OFF"))
						onPressed: hud.toggleMapViewport()
					}
					CommandButton {
						width: parent.width
						height: 36
						label: qsTr("Back")
						onPressed: hud.setMenuPage("main")
					}
				}
			}
		}
	}
}
