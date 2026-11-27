// Copyright 2026+ the openage authors. See copying.md for legal info.

module;

#include <memory>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>

#include "renderer/gui/guisys/private/livereload/recursive_directory_watcher_worker.h"
#include "renderer/gui/guisys/public/gui_application.h"
#include "renderer/gui/guisys/public/gui_engine.h"
#include "renderer/gui/guisys/public/gui_input.h"
#include "renderer/gui/guisys/public/gui_renderer.h"
#include "renderer/gui/guisys/public/gui_subtree.h"
#include "renderer/renderer.h"
#include "renderer/resources/texture_info.h"
#include "renderer/window.h"
#include "testing/testing.h"

export module openage.renderer.gui.guisys.livereload_tests;

export namespace qtgui::tests {

/** Native release cleanup must not cancel the offscreen scene's pointer grab. */
void native_input() {
	GuiApplication application;
	openage::renderer::window_settings settings;
	settings.width = 640;
	settings.height = 480;
	auto window = openage::renderer::Window::create("openage GUI input regression", settings);
	auto renderer = window->make_renderer();
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
	GuiInput input{gui};
	gui->set_texture(renderer->add_texture(openage::renderer::resources::Texture2dInfo{
		640, 480, openage::renderer::resources::pixel_format::rgba8}));
	window->add_mouse_button_callback([&input](const QMouseEvent &event) {
		input.process(std::shared_ptr<QEvent>{event.clone()});
	});
	window->add_key_callback([&input](const QKeyEvent &event) {
		input.process(std::shared_ptr<QEvent>{event.clone()});
	});
	application.process_events();
	gui->render();
	const auto roots = gui->get_window()->contentItem()->childItems();
	TESTEQUALS(roots.empty(), false);
	auto *root = roots.front();
	TESTEQUALS(root != nullptr, true);
	const QPointF point{100, 60};
	QMouseEvent press{QEvent::MouseButtonPress, point, window->get_qt_window()->mapToGlobal(point), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
	QCoreApplication::sendEvent(window->get_qt_window().get(), &press);
	window->update();
	gui->render();
	QMouseEvent release{QEvent::MouseButtonRelease, point, window->get_qt_window()->mapToGlobal(point), Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
	QCoreApplication::sendEvent(window->get_qt_window().get(), &release);
	// QGuiApplication clears the native device's grab after native release delivery.
	release.setExclusiveGrabber(release.point(0), nullptr);
	window->update();
	gui->render();
	TESTEQUALS(root->property("clicks").toInt(), 1);
	auto *chat = root->findChild<QQuickItem *>("chat");
	TESTEQUALS(chat != nullptr, true);
	chat->forceActiveFocus();
	QKeyEvent key{QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a"};
	QCoreApplication::sendEvent(window->get_qt_window().get(), &key);
	window->update();
	TESTEQUALS(chat->property("text").toString().toStdString(), "a");
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
