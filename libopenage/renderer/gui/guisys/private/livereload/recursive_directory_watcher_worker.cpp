// Copyright 2015-2022 the openage authors. See copying.md for legal info.

#include "recursive_directory_watcher_worker.h"

#include <algorithm>
#include <utility>

#include <QDirIterator>
#include <QFileInfo>

namespace qtgui {

namespace {
const int batch_ms = 100;
}

RecursiveDirectoryWatcherWorker::RecursiveDirectoryWatcherWorker() :
	QObject{} {
	this->batching_timer.setInterval(batch_ms);
	this->batching_timer.setSingleShot(true);
	QObject::connect(&this->batching_timer, &QTimer::timeout, this, &RecursiveDirectoryWatcherWorker::restartWatching);
	QObject::connect(&this->watcher, &QFileSystemWatcher::directoryChanged, this, &RecursiveDirectoryWatcherWorker::onEntryChanged);
	QObject::connect(&this->watcher, &QFileSystemWatcher::fileChanged, this, &RecursiveDirectoryWatcherWorker::onEntryChanged);
}

void RecursiveDirectoryWatcherWorker::onRootDirsPathsChanged(const QStringList &root_dirs_paths) {
	if (this->root_dirs_paths != root_dirs_paths) {
		this->root_dirs_paths = root_dirs_paths;
		this->restartWatching();
	}
}

namespace {
QStringList collect_entries_to_watch(const QStringList &root_dirs_paths) {
	QStringList root_dirs_paths_no_duplicates = root_dirs_paths;
	root_dirs_paths_no_duplicates.removeDuplicates();

	QStringList entries_to_watch;

	std::for_each(std::begin(root_dirs_paths_no_duplicates), std::end(root_dirs_paths_no_duplicates), [&entries_to_watch](const QString &root_dir_path) {
		entries_to_watch.append(root_dir_path);
		QDirIterator it{root_dir_path, QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks};

		while (it.hasNext()) {
			entries_to_watch.append(it.next());
		}
	});

	entries_to_watch.removeDuplicates();
	return entries_to_watch;
}
} // namespace

void RecursiveDirectoryWatcherWorker::restartWatching() {
	const auto paths = collect_entries_to_watch(this->root_dirs_paths);
	QMap<QString, QPair<qint64, QDateTime>> entries;
	for (const auto &path : paths) {
		const QFileInfo info{path};
		entries.insert(path, {info.size(), info.lastModified()});
	}
	const bool changed = this->initialized and this->entries != entries;
	this->entries = std::move(entries);
	this->initialized = true;
	this->restart_watching(paths);
	// Backends can report registration or access events without a source edit.
	if (changed) {
		emit this->changeDetected();
	}
}

void RecursiveDirectoryWatcherWorker::restart_watching(const QStringList &entries_to_watch) {
	// Recreating the macOS watcher replays filesystem events and triggers another reload.
	const auto watched = this->watcher.directories() + this->watcher.files();
	for (const auto &path : watched) {
		if (not entries_to_watch.contains(path)) {
			this->watcher.removePath(path);
		}
	}
	for (const auto &path : entries_to_watch) {
		if (not watched.contains(path)) {
			this->watcher.addPath(path);
		}
	}
}

void RecursiveDirectoryWatcherWorker::onEntryChanged() {
	if (!this->batching_timer.isActive())
		this->batching_timer.start();
}

} // namespace qtgui
