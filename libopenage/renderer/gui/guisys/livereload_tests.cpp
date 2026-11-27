// Copyright 2026+ the openage authors. See copying.md for legal info.

module;

#include <memory>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QResizeEvent>
#include <QTemporaryDir>
#include <QWheelEvent>

#include "renderer/gui/guisys/private/livereload/recursive_directory_watcher_worker.h"
#include "renderer/gui/guisys/public/gui_application.h"
#include "renderer/gui/guisys/public/gui_engine.h"
#include "renderer/gui/guisys/public/gui_renderer.h"
#include "renderer/gui/guisys/public/gui_subtree.h"
#include "renderer/window.h"
#include "testing/testing.h"

export module openage.renderer.gui.guisys.livereload_tests;

import openage.util.startup_trace;

export namespace qtgui::tests {

/** QQuickWindow must handle QML input natively and forward only unhandled events to game callbacks. */
void native_input() {
	openage::util::StartupTraceScope trace{"test.gui_native_input"};
	TESTEQUALS(openage::util::query_process_memory().rss_bytes > 0, true);

	GuiApplication application;
	openage::renderer::window_settings settings;
	settings.width = 640;
	settings.height = 480;
	auto window = openage::renderer::Window::create("openage GUI input regression", settings);
	auto gui = std::make_shared<GuiRenderer>(window);
	auto engine = std::make_shared<GuiQmlEngine>(gui, nullptr, nullptr);
	QTemporaryDir temporary;
	TESTEQUALS(temporary.isValid(), true);
	QFile source{temporary.filePath("main.qml")};
	TESTEQUALS(source.open(QIODevice::WriteOnly), true);
	const QByteArray qml = R"(import QtQuick
Item {
    objectName: "inputRoot"
    property int clicks: 0
    MouseArea { x: 40; y: 40; width: 120; height: 40; onClicked: parent.clicks++ }
    TextInput { objectName: "chat"; x: 40; y: 120; width: 200; height: 40 }
})";
	TESTEQUALS(source.write(qml), qml.size());
	source.close();
	GuiSubtree subtree{gui, engine, source.fileName().toStdString(), temporary.path().toStdString()};
	int game_presses = 0;
	int game_moves = 0;
	int game_releases = 0;
	int game_key_releases = 0;
	int game_wheels = 0;
	int leave_events = 0;
	int deactivate_events = 0;
	window->add_mouse_button_callback([&](const QMouseEvent &event) {
		if (event.type() == QEvent::MouseButtonPress) {
			++game_presses;
		}
		else if (event.type() == QEvent::MouseButtonRelease) {
			++game_releases;
		}
	});
	window->add_mouse_move_callback([&game_moves](const QMouseEvent &) {
		++game_moves;
	});
	window->add_mouse_wheel_callback([&game_wheels](const QWheelEvent &) {
		++game_wheels;
	});
	window->add_focus_lost_callback([&](bool deactivated) {
		if (deactivated) {
			++deactivate_events;
		}
		else {
			++leave_events;
		}
	});
	window->add_key_callback([&game_key_releases](const QKeyEvent &event) {
		if (event.type() == QEvent::KeyRelease) {
			++game_key_releases;
		}
	});
	application.process_events();
	const auto roots = gui->get_window()->contentItem()->childItems();
	TESTEQUALS(roots.empty(), false);
	auto *root = roots.front();
	TESTEQUALS(root != nullptr, true);

	// Click inside QML MouseArea: consumed by QML, not passed to game callback.
	const QPointF button_point{100, 60};
	QMouseEvent press{QEvent::MouseButtonPress, button_point, window->get_qt_window()->mapToGlobal(button_point), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
	QCoreApplication::sendEvent(window->get_qt_window().get(), &press);
	QMouseEvent release{QEvent::MouseButtonRelease, button_point, window->get_qt_window()->mapToGlobal(button_point), Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
	QCoreApplication::sendEvent(window->get_qt_window().get(), &release);
	window->update();
	TESTEQUALS(root->property("clicks").toInt(), 1);
	TESTEQUALS(game_presses, 0);
	TESTEQUALS(game_releases, 0);

	// Drag outside QML controls: press, move, and release must all reach game callbacks.
	const QPointF empty_point{400, 300};
	const QPointF drag_point{460, 360};
	QMouseEvent map_press{QEvent::MouseButtonPress, empty_point, window->get_qt_window()->mapToGlobal(empty_point), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
	QCoreApplication::sendEvent(window->get_qt_window().get(), &map_press);
	QMouseEvent map_move{QEvent::MouseMove, drag_point, window->get_qt_window()->mapToGlobal(drag_point), Qt::NoButton, Qt::LeftButton, Qt::NoModifier};
	QCoreApplication::sendEvent(window->get_qt_window().get(), &map_move);
	QMouseEvent map_release{QEvent::MouseButtonRelease, drag_point, window->get_qt_window()->mapToGlobal(drag_point), Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
	QCoreApplication::sendEvent(window->get_qt_window().get(), &map_release);
	window->update();
	TESTEQUALS(game_presses, 1);
	TESTEQUALS(game_moves >= 1, true);
	TESTEQUALS(game_releases, 1);

	auto *chat = root->findChild<QQuickItem *>("chat");
	TESTEQUALS(chat != nullptr, true);
	chat->forceActiveFocus();
	QKeyEvent key{QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a"};
	QCoreApplication::sendEvent(window->get_qt_window().get(), &key);
	window->update();
	TESTEQUALS(chat->property("text").toString().toStdString(), "a");

	// Key release outside a focused TextInput must reach game callbacks.
	chat->setFocus(false);
	QKeyEvent key_release{QEvent::KeyRelease, Qt::Key_W, Qt::NoModifier};
	QCoreApplication::sendEvent(window->get_qt_window().get(), &key_release);
	window->update();
	TESTEQUALS(game_key_releases, 1);

	// Wheel over empty map area must reach game callbacks.
	QWheelEvent wheel{empty_point, window->get_qt_window()->mapToGlobal(empty_point),
	                  QPoint{}, QPoint{0, 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false};
	QCoreApplication::sendEvent(window->get_qt_window().get(), &wheel);
	window->update();
	TESTEQUALS(game_wheels, 1);

	// Pointer leave and window focus loss must fire focus_lost callbacks.
	QEvent leave{QEvent::Leave};
	QFocusEvent focus_out{QEvent::FocusOut, Qt::ActiveWindowFocusReason};
	QCoreApplication::sendEvent(window->get_qt_window().get(), &leave);
	QCoreApplication::sendEvent(window->get_qt_window().get(), &focus_out);
	window->update();
	TESTEQUALS(leave_events, 1);
	TESTEQUALS(deactivate_events, 1);

	// 0x0 resizes (e.g. window minimize) must be ignored without corrupting cached size.
	window->set_size(0, 0);
	QResizeEvent zero_resize{QSize{0, 0}, QSize{640, 480}};
	QCoreApplication::sendEvent(window->get_qt_window().get(), &zero_resize);
	window->update();
	TESTEQUALS(window->get_size()[0], 640);
	TESTEQUALS(window->get_size()[1], 480);
}

/** Source rescans must preserve clicks unless a watched source actually changes. */
void source_changes() {
	int argc = 1;
	char name[] = "openage-watcher-test";
	char *argv[] = {name, nullptr};
	std::unique_ptr<QCoreApplication> application;
	if (not QCoreApplication::instance()) {
		application = std::make_unique<QCoreApplication>(argc, argv);
	}
	QTemporaryDir temporary;
	TESTEQUALS(temporary.isValid(), true);
	QDir directory{temporary.path()};
	TESTEQUALS(directory.mkdir("qml"), true);
	const auto root = directory.filePath("qml");
	QFile source{root + "/main.qml"};
	TESTEQUALS(source.open(QIODevice::WriteOnly), true);
	TESTEQUALS(source.write("import QtQuick\nItem {}\n"), 23);
	source.close();

	RecursiveDirectoryWatcherWorker watcher;
	int changes = 0;
	QObject::connect(&watcher, &RecursiveDirectoryWatcherWorker::changeDetected, [&changes] {
		++changes;
	});
	watcher.onRootDirsPathsChanged({root});
	watcher.restartWatching();
	watcher.restartWatching();
	TESTEQUALS(changes, 0);

	TESTEQUALS(source.open(QIODevice::Append), true);
	TESTEQUALS(source.write("// edited\n"), 10);
	source.close();
	watcher.restartWatching();
	TESTEQUALS(changes, 1);
	watcher.restartWatching();
	TESTEQUALS(changes, 1);

	// Changes beside the QML tree must not invalidate the interface.
	QFile sibling{directory.filePath("unrelated")};
	TESTEQUALS(sibling.open(QIODevice::WriteOnly), true);
	TESTEQUALS(sibling.write("unrelated"), 9);
	sibling.close();
	watcher.restartWatching();
	TESTEQUALS(changes, 1);

	TESTEQUALS(QDir{root}.mkdir("components"), true);
	QFile nested{root + "/components/Button.qml"};
	TESTEQUALS(nested.open(QIODevice::WriteOnly), true);
	TESTEQUALS(nested.write("import QtQuick\nItem {}\n"), 23);
	nested.close();
	watcher.restartWatching();
	TESTEQUALS(changes, 2);
	TESTEQUALS(nested.remove(), true);
	watcher.restartWatching();
	TESTEQUALS(changes, 3);
	watcher.restartWatching();
	TESTEQUALS(changes, 3);
}

} // namespace qtgui::tests
