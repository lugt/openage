// Copyright 2018-2024 the openage authors. See copying.md for legal info.

#pragma once

#include "renderer/window.h"

#include <optional>


QT_FORWARD_DECLARE_CLASS(QWindow)
QT_FORWARD_DECLARE_CLASS(QQuickWindow)
QT_FORWARD_DECLARE_CLASS(QOpenGLContext)

namespace openage {
namespace renderer {

namespace opengl {

class GlContext;

class GlWindow final : public Window {
public:
	/**
	 * Create a shiny window with the given title.
	 *
	 * @param title The window title.
	 * @param settings Settings for creating the window.
	 */
	GlWindow(const std::string &title,
	         window_settings settings = {});
	~GlWindow();

	void set_size(size_t width, size_t height) override;

	void update() override;

	std::shared_ptr<Renderer> make_renderer() override;

	/// Make this window's context the current rendering context of the current thread.
	/// Only use this and most other GL functions on a dedicated window thread.
	void make_context_current();

	/// Release this window's context from the current thread.
	/// Only use this and most other GL functions on a dedicated window thread.
	void done_context_current();

	/// Return a pointer to this window's GL context.
	const std::shared_ptr<opengl::GlContext> &get_context() const;

	/// Return the underlying QQuickWindow used for both OpenGL underlay and QML overlay.
	const std::shared_ptr<QQuickWindow> &get_quick_window() const;

private:
	// Main Qt Quick window (same object as Window::window)
	std::shared_ptr<QQuickWindow> quick_window;

	// OpenGL Context owned by QQuickWindow and wrapped by GlContext
	std::shared_ptr<opengl::GlContext> context;
};

} // namespace opengl
} // namespace renderer
} // namespace openage
