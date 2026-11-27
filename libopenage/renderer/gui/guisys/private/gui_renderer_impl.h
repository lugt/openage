// Copyright 2015-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <cstddef>
#include <memory>

#include <QObject>
#include <QQuickWindow>
#include <QSize>

namespace openage::renderer {
class Window;
} // namespace openage::renderer

namespace qtgui {

class GuiRenderer;


/**
 * Connects a Qt Quick QML scene subtree to the application's main QQuickWindow.
 */
class GuiRendererImpl final : public QObject {
	Q_OBJECT

public:
	explicit GuiRendererImpl(std::shared_ptr<openage::renderer::Window> window);
	~GuiRendererImpl() = default;

	static GuiRendererImpl *impl(GuiRenderer *renderer);

	/**
	 * Notify the attached QML root item about a window size change.
	 *
	 * @param width Target logical width.
	 * @param height Target logical height.
	 */
	void resize(size_t width, size_t height);

	/**
	 * Get the main QQuickWindow hosting the QML scene.
	 */
	std::shared_ptr<QQuickWindow> get_window();

signals:
	/**
	 * Emitted when the host window was resized.
	 */
	void resized(const QSize &size);

private:
	/**
	 * Main QQuickWindow owned by openage::renderer::Window.
	 */
	std::shared_ptr<QQuickWindow> target_window;
};

} // namespace qtgui
