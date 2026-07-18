// Copyright 2015-2025 the openage authors. See copying.md for legal info.

#include "gui_application_impl.h"

#include <cassert>
#include <locale>

#include <QSurfaceFormat>
#include <QtDebug>
#include <QtGlobal>

namespace qtgui {

std::weak_ptr<GuiApplicationImpl> GuiApplicationImpl::instance;

std::shared_ptr<GuiApplicationImpl> GuiApplicationImpl::get() {
	std::shared_ptr<GuiApplicationImpl> candidate = GuiApplicationImpl::instance.lock();

	assert(!candidate || std::this_thread::get_id() == candidate->owner);

	// Ensure that OpenGL is used and not OpenGL ES.
	// This occurred in macos. See issue #1177 (PR #1179)
	if (!candidate) {
		QSurfaceFormat format;
		format.setRenderableType(QSurfaceFormat::OpenGL);
		QSurfaceFormat::setDefaultFormat(format);

		// Report the exact (possibly fractional) devicePixelRatio instead of
		// the platform-default rounded value, so HiDPI scaling math uses the
		// real compositor scale. Must be set before QGuiApplication is
		// constructed (which happens in the GuiApplicationImpl member
		// initializer list below). On non-HiDPI (DPR==1.0) and integer-scale
		// (e.g. 2.0) displays this is a no-op; it only matters for fractional
		// scales (1.25x / 1.5x / 1.75x).
		QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
			Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
	}

	return candidate ? candidate : std::shared_ptr<GuiApplicationImpl>{new GuiApplicationImpl};
}

GuiApplicationImpl::~GuiApplicationImpl() {
	assert(std::this_thread::get_id() == this->owner);
}

void GuiApplicationImpl::processEvents() {
	assert(std::this_thread::get_id() == this->owner);
	this->app.processEvents();
}

namespace {
int argc = 1;
char arg[] = "qtgui";
char *argv = &arg[0];
} // namespace

GuiApplicationImpl::GuiApplicationImpl() :
#ifndef NDEBUG
	owner{std::this_thread::get_id()},
#endif
	app{argc, &argv} {
	// Set locale back to POSIX for the decimal point parsing (see qcoreapplication.html#locale-settings).
	std::locale::global(std::locale().combine<std::numpunct<char>>(std::locale::classic()));

	qInfo() << "Compiled with Qt" << QT_VERSION_STR << "and run with Qt" << qVersion();
}

} // namespace qtgui
