// Copyright 2015-2026 the openage authors. See copying.md for legal info.

#include "gui_renderer.h"

#include "renderer/gui/guisys/private/gui_renderer_impl.h"

namespace qtgui {

GuiRenderer::GuiRenderer(const std::shared_ptr<openage::renderer::Window> &window) :
	impl{std::make_unique<GuiRendererImpl>(window)} {
}

GuiRenderer::~GuiRenderer() = default;

void GuiRenderer::resize(int w, int h) {
	this->impl->resize(w, h);
}

std::shared_ptr<QQuickWindow> GuiRenderer::get_window() {
	return this->impl->get_window();
}

} // namespace qtgui
