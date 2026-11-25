// Copyright 2026+ the openage authors. See copying.md for legal info.

import QtQuick

Item {
	id: root

	property string source: ""
	signal activated(real x, real y)

	property int front: 0
	property bool initialized: false

	function refresh() {
		if (!initialized)
			return
		if (source.length === 0) {
			images.itemAt(0).source = ""
			images.itemAt(1).source = ""
			return
		}
		const current = images.itemAt(front) as Image
		if (current.status === Image.Ready && current.source.toString() === source)
			return
		const nextIndex = 1 - front
		const next = images.itemAt(nextIndex) as Image
		// Keep the current image visible and coalesce updates while decoding.
		if (next.status === Image.Loading)
			return
		next.source = source
		if (next.status === Image.Ready)
			front = nextIndex
	}

	onSourceChanged: refresh()
	Component.onCompleted: {
		initialized = true
		refresh()
	}

	Repeater {
		id: images
		model: [root, root]
		Image {
			required property int index
			required property var modelData
			anchors.fill: parent
			z: modelData.front === index ? 1 : 0
			visible: status === Image.Ready
			fillMode: Image.Stretch
			asynchronous: false
			cache: true
			smooth: false
			onStatusChanged: {
				if (status === Image.Ready) {
					if (source.toString() === modelData.source)
						modelData.front = index
					Qt.callLater(modelData.refresh)
				}
			}
		}
	}
	MouseArea {
		anchors.fill: parent
		enabled: root.source.length > 0
		onPressed: mouse => root.activated(mouse.x / width, mouse.y / height)
	}
}
