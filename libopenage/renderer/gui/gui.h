// Copyright 2015-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <memory>

#include "renderer/gui/guisys/public/gui_subtree.h"

class QObject;

namespace qtgui {
class GuiRenderer;
class GuiQmlEngine;
class GuiApplication;
} // namespace qtgui

namespace openage {
namespace util {
class Path;
}

namespace renderer {
class Window;

namespace gui {

/**
 * Interface (= HUD) of the game.
 *
 * openage's GUI is Qt Quick (QML) mounted directly onto the main QQuickWindow
 * so Qt renders the HUD over the OpenGL game underlay in the same context.
 */
class GUI {
public:
	explicit GUI(std::shared_ptr<qtgui::GuiApplication> app,
	             std::shared_ptr<Window> window,
	             const util::Path &source,
	             const util::Path &rootdir,
	             const char *context_property_name,
	             QObject *context_property);
	virtual ~GUI() = default;

private:
	/**
	 * Propagate logical window resizes to the QML root item.
	 */
	void resize(size_t width, size_t height);

	/**
	 * Reference to the Qt GUI application singleton.
	 */
	std::shared_ptr<qtgui::GuiApplication> application;

	/**
	 * Adapter exposing the host QQuickWindow to the QML engine and subtree.
	 */
	std::shared_ptr<qtgui::GuiRenderer> gui_renderer;

	/**
	 * Qt QML Engine wrapper.
	 */
	std::shared_ptr<qtgui::GuiQmlEngine> engine;

	/**
	 * Manages Qt QML components and items.
	 */
	qtgui::GuiSubtree subtree;
};

} // namespace gui
} // namespace renderer
} // namespace openage
