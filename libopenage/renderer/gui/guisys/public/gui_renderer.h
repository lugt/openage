// Copyright 2015-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <memory>

#include <QObject>

QT_FORWARD_DECLARE_CLASS(QQuickWindow)

namespace openage::renderer {
class Window;
} // namespace openage::renderer

namespace qtgui {

class GuiRendererImpl;

/**
 * Exposes the host QQuickWindow to the QML engine and subtree.
 */
class GuiRenderer {
public:
	explicit GuiRenderer(const std::shared_ptr<openage::renderer::Window> &window);
	~GuiRenderer();

	void resize(int w, int h);
	std::shared_ptr<QQuickWindow> get_window();

private:
	friend class GuiRendererImpl;
	std::unique_ptr<GuiRendererImpl> impl;
};

} // namespace qtgui
