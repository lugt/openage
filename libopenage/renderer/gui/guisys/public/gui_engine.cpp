// Copyright 2015-2022 the openage authors. See copying.md for legal info.

#include "renderer/gui/guisys/public/gui_engine.h"

#include <QQmlContext>
#include <QQmlEngine>

#include "renderer/gui/guisys/private/gui_engine_impl.h"

namespace qtgui {

GuiQmlEngine::GuiQmlEngine(std::shared_ptr<GuiRenderer> renderer,
                           const char *context_property_name,
                           QObject *context_property) :
	impl{std::make_unique<GuiQmlEngineImpl>(renderer)} {
	if (context_property) {
		this->impl->get_qml_engine()->rootContext()->setContextProperty(
			QString::fromUtf8(context_property_name), context_property);
	}
}

GuiQmlEngine::~GuiQmlEngine() = default;

} // namespace qtgui
