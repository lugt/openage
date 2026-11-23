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
					{ label: qsTr("Wood"), value: hud.wood },
					{ label: qsTr("Food"), value: hud.food },
					{ label: qsTr("Gold"), value: hud.gold },
					{ label: qsTr("Stone"), value: hud.stone },
					{ label: qsTr("Population"), value: qsTr("%1/%2").arg(hud.population).arg(hud.populationCapacity) }
				]
				delegate: Item {
					id: counter
					required property var modelData
					required property int index
					width: (economyBar.width - 2) / 5
					height: economyBar.height - 2

					Rectangle {
						anchors.right: parent.right
						anchors.verticalCenter: parent.verticalCenter
						width: 1
						height: parent.height - 12
						color: "#514535"
						visible: counter.index < 4
					}
					Column {
						width: Math.max(0, parent.width - 8)
						anchors.centerIn: parent
						spacing: 2
						Text {
							width: parent.width
							text: counter.modelData.label
							horizontalAlignment: Text.AlignHCenter
							font.pixelSize: 11
							elide: Text.ElideRight
							color: "#a7b7c5"
						}
						Text {
							width: parent.width
							text: counter.modelData.value
							horizontalAlignment: Text.AlignHCenter
							font.pixelSize: 15
							font.bold: true
							fontSizeMode: Text.HorizontalFit
							minimumPixelSize: 9
							color: textColor
						}
					}
				}
			}
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

	component CommandButton: Rectangle {
		id: button
		required property string label
		property bool active: false
		signal pressed
		width: 86
		height: 34
		radius: 3
		border.color: enabled && (active || control.containsMouse) ? "#d6b677" : "#5c5040"
		color: enabled ? (active || control.pressed ? "#645037" : "#2b2a29") : "#222020"
		Text {
			anchors.centerIn: parent
			width: Math.max(0, parent.width - 8)
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
			hoverEnabled: true
			onClicked: button.pressed()
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
			anchors.left: parent.left
			anchors.top: parent.top
			anchors.bottom: parent.bottom
			anchors.margins: 4
			width: Math.min(174, root.width * 0.20)
			color: "#191918"
			border.color: "#625441"
			Flickable {
				anchors.fill: parent
				anchors.margins: 5
				contentHeight: commandList.implicitHeight
				contentWidth: width
				clip: true
				boundsBehavior: Flickable.StopAtBounds
				Column {
					id: commandList
					width: parent.width
					spacing: 4
					Row {
						spacing: 4
						CommandButton {
							width: (commandList.width - 4) / 2
							label: qsTr("Stop")
							enabled: hud.canStop
							onPressed: hud.stopSelected()
						}
						CommandButton {
							width: (commandList.width - 4) / 2
							label: qsTr("Deselect")
							enabled: hud.selectionCards.length > 0
							onPressed: hud.clearSelection()
						}
					}
					Repeater {
						model: hud.actionOptions
						delegate: CommandButton {
							required property var modelData
							width: commandList.width
							label: (modelData.build ? qsTr("Build %1") : modelData.research ? qsTr("Research %1") : qsTr("Train %1"))
							       .arg(modelData.name) + (modelData.detail ? " · " + modelData.detail : "")
							onPressed: hud.requestAction(modelData.id, modelData.build)
						}
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
				Image {
					id: minimap
					anchors.fill: parent
					anchors.margins: 2
					source: hud.minimap
					fillMode: Image.Stretch
					cache: false
					smooth: false
					MouseArea {
						anchors.fill: parent
						enabled: hud.minimap.length > 0
						onPressed: mouse => hud.moveCamera(mouse.x / width, mouse.y / height)
					}
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
				clip: true
				boundsBehavior: Flickable.StopAtBounds

				Column {
					id: details
					width: parent.width
					spacing: 5

					Text {
						readonly property var card: hud.selectionCards.length ? hud.selectionCards[0] : null
						text: !card ? qsTr("No selection")
						      : hud.selectionCards.length > 1 ? qsTr("%1 selected").arg(hud.selectionCards.length)
						      : card.name + "  ·  " + card.owner
						color: "#37281c"
						font.bold: true
						font.pixelSize: 16
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
								Text {
									anchors.centerIn: parent
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
									width: (parent.width - 4) * (card.modelData.maxHp > 0
									       ? Math.max(0, Math.min(1, card.modelData.hp / card.modelData.maxHp)) : 1)
									color: card.modelData.color
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
}
