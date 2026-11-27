// Copyright 2015-2023 the openage authors. See copying.md for legal info.

#include "gui_input_impl.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QHoverEvent>
#include <QQuickWindow>

#include "renderer/gui/guisys/public/gui_renderer.h"

namespace qtgui {

GuiInputImpl::GuiInputImpl(const std::shared_ptr<GuiRenderer> &renderer) :
	QObject{},
	window{renderer->get_window()},
	mouse_device{"openage GUI mouse", 0, QInputDevice::DeviceType::Mouse,
	             QPointingDevice::PointerType::Generic,
	             QInputDevice::Capability::Position | QInputDevice::Capability::Hover, 1, 3} {
}


bool GuiInputImpl::process(const std::shared_ptr<QEvent> &event) {
	if (this->window and not this->window->isActive()
	    and (event->type() == QEvent::MouseButtonPress
	         or event->type() == QEvent::KeyPress)) {
		QFocusEvent focus_in{QEvent::FocusIn, Qt::ActiveWindowFocusReason};
		QCoreApplication::instance()->sendEvent(this->window.get(), &focus_in);
	}
	switch (event->type()) {
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonRelease:
	case QEvent::MouseMove:
	case QEvent::MouseButtonDblClick: {
		// Quote from the QQuickRenderControl Example:
		// Use the constructor taking position and globalPosition. That puts position into the
		// event's position and scenePosition, and globalPosition into the event's globalPosition. This way
		// the scenePosition in e is ignored and is replaced by position. This is necessary
		// because QQuickWindow thinks of itself as a top-level window always.
		auto mouse_event = std::dynamic_pointer_cast<QMouseEvent>(event);
		const QPointF pos = mouse_event->position();
		const QPointF global_pos = mouse_event->globalPosition();
		const QPointF old_pos = this->last_mouse_pos.x() >= 0.0 ? this->last_mouse_pos : pos;
		if (event->type() == QEvent::MouseMove or pos != this->last_mouse_pos) {
			QHoverEvent hover_ev{QEvent::HoverMove, pos, global_pos, old_pos, mouse_event->modifiers(), &this->mouse_device};
			QCoreApplication::instance()->sendEvent(this->window.get(), &hover_ev);
			this->last_mouse_pos = pos;
		}
		// Native release delivery clears grabs before queued events reach QML.
		// Keep the offscreen scene's grab state separate from the native mouse.
		QMouseEvent ev{mouse_event->type(),
		               pos,
		               global_pos,
		               mouse_event->button(),
		               mouse_event->buttons(),
		               mouse_event->modifiers(),
		               &this->mouse_device};
		ev.setTimestamp(mouse_event->timestamp());
		return QCoreApplication::instance()->sendEvent(this->window.get(), &ev);
	}
	case QEvent::KeyPress:
	case QEvent::KeyRelease:
		return QCoreApplication::instance()->sendEvent(this->window.get(), event.get());

	case QEvent::Wheel:
		return QCoreApplication::instance()->sendEvent(this->window.get(), event.get());

	default:
		break;
	}
	return false;
}

} // namespace qtgui
