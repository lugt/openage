// Copyright 2015-2026 the openage authors. See copying.md for legal info.

#include "gui_renderer_impl.h"

#include <QQuickWindow>

#include "error/error.h"
#include "log/log.h"
#include "log/message.h"

#include "renderer/gui/guisys/public/gui_renderer.h"
#include "renderer/window.h"


namespace qtgui {


GuiRendererImpl::GuiRendererImpl(std::shared_ptr<openage::renderer::Window> window) :
	QObject{},
	target_window{window ? std::dynamic_pointer_cast<QQuickWindow>(window->get_qt_window()) : nullptr} {
	ENSURE(window != nullptr, "GuiRendererImpl requires a non-null Window.");
	ENSURE(this->target_window != nullptr, "GUI requires a Window backed by QQuickWindow.");
}


std::shared_ptr<QQuickWindow> GuiRendererImpl::get_window() {
	return this->target_window;
}


GuiRendererImpl *GuiRendererImpl::impl(GuiRenderer *renderer) {
	return renderer->impl.get();
}


void GuiRendererImpl::resize(const size_t width, const size_t height) {
	if (width == 0 or height == 0) {
		openage::log::log(MSG(dbg) << "Ignoring 0x0 GUI resize ("
		                           << width << "x" << height << ")");
		return;
	}
	emit this->resized(QSize(static_cast<int>(width), static_cast<int>(height)));
}


} // namespace qtgui
