// Copyright 2015-2026 the openage authors. See copying.md for legal info.

#include "gui.h"

#include "error/error.h"
#include "renderer/gui/guisys/public/gui_engine.h"
#include "renderer/gui/guisys/public/gui_renderer.h"
#include "renderer/gui/integration/public/gui_application_with_logger.h"
#include "renderer/window.h"
#include "util/path.h"

import openage.util.startup_trace;

namespace openage::renderer::gui {

GUI::GUI(std::shared_ptr<qtgui::GuiApplication> app,
         std::shared_ptr<Window> window,
         const util::Path &source,
         const util::Path &rootdir,
         const char *context_property_name,
         QObject *context_property) :
	application{std::move(app)},
	gui_renderer{std::make_shared<qtgui::GuiRenderer>(window)},
	engine{std::make_shared<qtgui::GuiQmlEngine>(
		this->gui_renderer, context_property_name, context_property)},
	subtree{
		this->gui_renderer,
		this->engine,
		source.resolve_native_path(),
		rootdir.resolve_native_path()} {
	util::StartupTraceScope trace{"gui.init"};
	ENSURE(this->application != nullptr and window != nullptr,
	       "GUI requires non-null GuiApplication and Window.");

	auto size = window->get_size();
	this->resize(size[0], size[1]);

	window->add_resize_callback([this](size_t width, size_t height, double /*scale*/) {
		this->resize(width, height);
	});
}


void GUI::resize(size_t width, size_t height) {
	this->gui_renderer->resize(static_cast<int>(width), static_cast<int>(height));
}

} // namespace openage::renderer::gui
