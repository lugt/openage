// Copyright 2026+ the openage authors. See copying.md for legal info.

module;

#include <memory>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "renderer/gui/guisys/private/livereload/recursive_directory_watcher_worker.h"
#include "testing/testing.h"

export module openage.renderer.gui.guisys.livereload_tests;

export namespace qtgui::tests {

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
