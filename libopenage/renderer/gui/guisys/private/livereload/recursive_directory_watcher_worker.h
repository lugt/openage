// Copyright 2015-2022 the openage authors. See copying.md for legal info.

#pragma once

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QMap>
#include <QStringList>
#include <QTimer>

namespace qtgui {

/**
 * Emits a signal when anything changes in the directories.
 */
class RecursiveDirectoryWatcherWorker : public QObject {
	Q_OBJECT

public:
	RecursiveDirectoryWatcherWorker();

signals:
	void changeDetected();

public slots:
	void onRootDirsPathsChanged(const QStringList &root_dirs_paths);
	void restartWatching();

private slots:
	void onEntryChanged();

private:
	void restart_watching(const QStringList &entries_to_watch);

	/**
	 * Actual watcher.
	 * Its event processing and destruction has to be in the same separate thread.
	 */
	QFileSystemWatcher watcher;

	/**
	 * Waits to glue multiple changes into one event.
	 */
	QTimer batching_timer;

	QStringList root_dirs_paths;
	QMap<QString, QPair<qint64, QDateTime>> entries;
	bool initialized{false};
};

} // namespace qtgui
