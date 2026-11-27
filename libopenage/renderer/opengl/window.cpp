// Copyright 2018-2026 the openage authors. See copying.md for legal info.

#include <epoxy/gl.h>

#include "window.h"

#include <chrono>
#include <optional>
#include <string>

#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QQuickWindow>
#include <QResizeEvent>
#include <QSGRendererInterface>
#include <QString>
#include <QWheelEvent>

#include "error/error.h"
#include "log/log.h"

#include "renderer/opengl/context.h"
#include "renderer/opengl/renderer.h"
#include "renderer/window_event_handler.h"

import openage.util.startup_trace;


namespace openage::renderer::opengl {

namespace {

double sanitize_dpr(double dpr) {
	if (dpr <= 0.0) {
		log::log(MSG(warn) << "Invalid devicePixelRatio (" << dpr << "); falling back to 1.0");
		return 1.0;
	}
	return dpr;
}

class ExternalCommandsGuard final {
public:
	ExternalCommandsGuard(QQuickWindow *window,
	                      std::shared_ptr<GlContext> context,
	                      bool restore_default_fbo) :
		window{window},
		context{std::move(context)},
		restore_default_fbo{restore_default_fbo} {
		this->window->beginExternalCommands();
	}

	~ExternalCommandsGuard() {
		if (this->restore_default_fbo and this->context and this->context->get_raw_context()) {
			glBindFramebuffer(GL_FRAMEBUFFER, this->context->get_default_framebuffer_id());
		}
		this->window->endExternalCommands();
	}

	ExternalCommandsGuard(const ExternalCommandsGuard &) = delete;
	ExternalCommandsGuard &operator=(const ExternalCommandsGuard &) = delete;

private:
	QQuickWindow *window;
	std::shared_ptr<GlContext> context;
	bool restore_default_fbo;
};

/**
 * QQuickWindow subclass that lets Qt Quick process input events natively first
 * and queues only events that QML did not accept for the game input pipeline.
 */
class QuickGlWindow final : public QQuickWindow {
public:
	explicit QuickGlWindow(std::shared_ptr<WindowEventHandler> event_handler) :
		QQuickWindow{},
		event_handler{std::move(event_handler)} {}

protected:
	bool event(QEvent *ev) override {
		const auto type = ev->type();
		const bool is_input =
			type == QEvent::KeyPress
			or type == QEvent::KeyRelease
			or type == QEvent::MouseButtonPress
			or type == QEvent::MouseButtonRelease
			or type == QEvent::MouseButtonDblClick
			or type == QEvent::MouseMove
			or type == QEvent::Wheel;

		const bool is_touch =
			type == QEvent::TouchBegin
			or type == QEvent::TouchUpdate
			or type == QEvent::TouchEnd;

		if (is_input or is_touch) {
			// QEvent starts accepted by default in Qt6; clear it so unhandled
			// MouseButtonRelease/MouseMove/KeyRelease and touch events remain
			// unaccepted (allowing Qt to synthesize mouse fallback for touch).
			ev->setAccepted(false);
		}

		const bool result = QQuickWindow::event(ev);

		if (is_input and not ev->isAccepted() and this->event_handler) {
			this->event_handler->enqueue(std::shared_ptr<QEvent>(ev->clone()));
		}

		return result;
	}

private:
	std::shared_ptr<WindowEventHandler> event_handler;
};

} // namespace

GlWindow::GlWindow(const std::string &title,
                   window_settings settings) :
	Window{settings.width, settings.height, settings.ignore_user_input, false} {
	util::StartupTraceScope total_trace{"window.total"};

	ENSURE(settings.width > 0 and settings.height > 0,
	       "Window dimensions must be non-zero.");
	if (QGuiApplication::instance() == nullptr) {
		// Qt windows need to attach to a QGuiApplication
		throw Error{MSG(err) << "Failed to create Qt window: QGuiApplication has not been created yet."};
	}

	QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

	auto quick_win = std::make_shared<QuickGlWindow>(this->event_handler);
	this->quick_window = quick_win;
	this->window = quick_win;

	this->quick_window->setTitle(QString::fromStdString(title));
	this->quick_window->resize(settings.width, settings.height);
	this->quick_window->setColor(Qt::black);

	gl_context_spec gl_specs{};
	{
		util::StartupTraceScope spec_trace{"window.gl_find_spec"};
		gl_specs = GlContext::find_spec();
	}

	QSurfaceFormat format{};
	format.setProfile(QSurfaceFormat::OpenGLContextProfile::CoreProfile);
	format.setSwapBehavior(QSurfaceFormat::SwapBehavior::DoubleBuffer);

	if (not settings.vsync) {
		format.setSwapInterval(0);
	}

	format.setMajorVersion(gl_specs.major_version);
	format.setMinorVersion(gl_specs.minor_version);

	format.setAlphaBufferSize(8);
	format.setDepthBufferSize(24);
	format.setStencilBufferSize(8);

	if (settings.debug) {
		format.setOption(QSurfaceFormat::DebugContext);
	}

	this->quick_window->setFormat(format);

	// Reset to a known display mode before showing.
	this->quick_window->setWindowState(Qt::WindowNoState);
	switch (settings.mode) {
	case window_mode::WINDOWED:
		break;
	case window_mode::BORDERLESS:
		this->quick_window->setFlags(this->quick_window->flags() | Qt::FramelessWindowHint);
		break;
	case window_mode::FULLSCREEN:
		this->quick_window->setWindowState(Qt::WindowFullScreen);
		break;
	default:
		throw Error{MSG(err) << "Invalid window mode."};
	}

	auto sg_error = std::make_shared<std::optional<std::string>>();

	QObject::connect(
		this->quick_window.get(),
		&QQuickWindow::sceneGraphInitialized,
		this->quick_window.get(),
		[this, debug = settings.debug]() {
			if (this->context) {
				return;
			}
			auto *rif = this->quick_window->rendererInterface();
			ENSURE(rif != nullptr, "QQuickWindow rendererInterface() returned null.");
			auto *raw_ctx = static_cast<QOpenGLContext *>(
				rif->getResource(this->quick_window.get(), QSGRendererInterface::OpenGLContextResource));
			if (raw_ctx == nullptr or not raw_ctx->isValid()) {
				throw Error{MSG(err) << "Failed to retrieve Qt Quick OpenGL context."};
			}
			this->context = std::make_shared<GlContext>(this->window, raw_ctx, debug);
		},
		Qt::DirectConnection);

	QObject::connect(
		this->quick_window.get(),
		&QQuickWindow::sceneGraphError,
		this->quick_window.get(),
		[sg_error](QQuickWindow::SceneGraphError error, const QString &message) {
			const std::string msg = message.toStdString();
			log::log(MSG(err) << "Qt Quick scene graph error (" << static_cast<int>(error)
			                  << "): " << msg);
			*sg_error = msg;
		},
		Qt::DirectConnection);

	QObject::connect(
		this->quick_window.get(),
		&QQuickWindow::sceneGraphInvalidated,
		this->quick_window.get(),
		[this]() {
			log::log(MSG(dbg) << "Qt Quick scene graph invalidated; releasing non-owning GlContext");
			this->context.reset();
		},
		Qt::DirectConnection);

	QObject::connect(
		this->quick_window.get(),
		&QQuickWindow::beforeRenderPassRecording,
		this->quick_window.get(),
		[this]() {
			if (not this->on_render or not this->context) {
				return;
			}
			ExternalCommandsGuard guard{this->quick_window.get(), this->context, true};
			this->context->reset_gl_state();
			this->on_render();
		},
		Qt::DirectConnection);

	QObject::connect(
		this->quick_window.get(),
		&QQuickWindow::afterRenderPassRecording,
		this->quick_window.get(),
		[this]() {
			if (not this->on_post_render or not this->context) {
				return;
			}
			ExternalCommandsGuard guard{this->quick_window.get(), this->context, false};
			this->on_post_render();
		},
		Qt::DirectConnection);

	this->quick_window->installEventFilter(this->event_handler.get());
	{
		util::StartupTraceScope sg_trace{"window.qt_scenegraph_init"};
		this->quick_window->setVisible(true);

		// Wait for the initial expose/sceneGraphInitialized cycle so the OpenGL
		// context is ready before make_renderer() and resource uploads run.
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
		while (not this->context and not sg_error->has_value()
		       and std::chrono::steady_clock::now() < deadline) {
			this->quick_window->update();
			QCoreApplication::processEvents(QEventLoop::AllEvents, 16);
		}
		if (sg_error->has_value()) {
			throw Error{MSG(err) << "Qt Quick scene graph initialization failed: "
			                     << sg_error->value()};
		}
		if (not this->context) {
			throw Error{MSG(err) << "Timed out waiting for Qt Quick OpenGL context initialization."};
		}
	}

	this->make_context_current();
	this->context->reset_gl_state();

	// Scaling factor if highDPI
	this->scale_dpr = sanitize_dpr(this->quick_window->devicePixelRatio());
	const auto live_specs = this->context->get_specs();
	log::log(MSG(info) << "Created Qt Quick window (" << this->size[0] << "x" << this->size[1]
	                   << ", DPR=" << this->scale_dpr
	                   << ", physical=" << static_cast<size_t>(this->size[0] * this->scale_dpr)
	                   << "x" << static_cast<size_t>(this->size[1] * this->scale_dpr)
	                   << ", OpenGL=" << live_specs.major_version << "." << live_specs.minor_version
	                   << ", vsync=" << (settings.vsync ? "on" : "off") << ")");

	GlContext::check_error();
}

GlWindow::~GlWindow() {
	this->on_render = nullptr;
	this->on_post_render = nullptr;
	if (this->quick_window) {
		QObject::disconnect(this->quick_window.get(), nullptr, this->quick_window.get(), nullptr);
		this->quick_window->removeEventFilter(this->event_handler.get());
		this->quick_window->hide();
	}
	if (this->context and this->context->get_raw_context() and this->quick_window) {
		this->make_context_current();
	}
}


void GlWindow::set_size(size_t width, size_t height) {
	if (width == 0 or height == 0) {
		log::log(MSG(dbg) << "Ignoring 0x0 window set_size request ("
		                  << width << "x" << height << ")");
		return;
	}

	// Re-read the live DPR: on Wayland the devicePixelRatio delivered by the
	// compositor's configure event may differ from the value cached at
	// construction. This keeps the physical framebuffer in sync with Qt's
	// actual window surface.
	this->scale_dpr = sanitize_dpr(this->quick_window->devicePixelRatio());

	if (this->size[0] != width || this->size[1] != height) {
		this->quick_window->resize(width, height);
		this->size = {width, height};
	}

	for (auto &cb : this->on_resize) {
		cb(width, height, this->scale_dpr);
	}
}


void GlWindow::update() {
	// Ensure the OpenGL context is current while dispatching callbacks
	// (e.g. picking or renderer uploads triggered from input/resize handlers).
	this->make_context_current();

	auto events = this->event_handler->pop_events();

	for (auto &&event : events) {
		switch (event->type()) {
		case QEvent::Resize: {
			auto ev = std::dynamic_pointer_cast<QResizeEvent>(event);
			if (not ev) {
				break;
			}
			const int raw_width = ev->size().width();
			const int raw_height = ev->size().height();
			if (raw_width <= 0 or raw_height <= 0) {
				log::log(MSG(dbg) << "Ignoring non-positive QEvent::Resize ("
				                  << raw_width << "x" << raw_height << ")");
				break;
			}
			const size_t width = static_cast<size_t>(raw_width);
			const size_t height = static_cast<size_t>(raw_height);

			const double old_scale = this->scale_dpr;
			this->scale_dpr = sanitize_dpr(this->quick_window->devicePixelRatio());
			if (old_scale != this->scale_dpr) {
				log::log(MSG(info) << "DPR changed: " << old_scale << " -> "
				                  << this->scale_dpr);
			}

			log::log(MSG(dbg) << "Window resized to: " << width << "x" << height
			                  << " (DPR=" << this->scale_dpr << ")");

			this->size = {width, height};
			for (auto &cb : this->on_resize) {
				cb(width, height, this->scale_dpr);
			}
		} break;
		case QEvent::Close: {
			log::log(MSG(info) << "Window close requested by user (QEvent::Close); shutting down");
			this->should_be_closed = true;
		} break;

		case QEvent::KeyPress:
		case QEvent::KeyRelease: {
			auto const ev = std::dynamic_pointer_cast<QKeyEvent>(event);
			if (ev) {
				for (auto &cb : this->on_key) {
					cb(*ev);
				}
			}
		} break;
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonRelease:
		case QEvent::MouseButtonDblClick: {
			auto const ev = std::dynamic_pointer_cast<QMouseEvent>(event);
			if (ev) {
				for (auto &cb : this->on_mouse_button) {
					cb(*ev);
				}
			}
		} break;
		case QEvent::MouseMove: {
			auto const ev = std::dynamic_pointer_cast<QMouseEvent>(event);
			if (ev) {
				for (auto &cb : this->on_mouse_move) {
					cb(*ev);
				}
			}
		} break;
		case QEvent::Wheel: {
			auto const ev = std::dynamic_pointer_cast<QWheelEvent>(event);
			if (ev) {
				for (auto &cb : this->on_mouse_wheel) {
					cb(*ev);
				}
			}
		} break;
		case QEvent::Leave: {
			for (auto &cb : this->on_focus_lost) {
				cb(false);
			}
		} break;
		case QEvent::FocusOut:
		case QEvent::WindowDeactivate: {
			for (auto &cb : this->on_focus_lost) {
				cb(true);
			}
		} break;
		case QEvent::ScreenChangeInternal: {
			const double old_scale = this->scale_dpr;
			this->scale_dpr = sanitize_dpr(this->quick_window->devicePixelRatio());
			log::log(MSG(info) << "Screen changed; DPR " << old_scale << " -> "
			                  << this->scale_dpr);
			for (auto &cb : this->on_resize) {
				cb(this->size[0], this->size[1], this->scale_dpr);
			}
		} break;
		default:
			break;
		}
	}

	if (this->on_render) {
		// In underlay mode Qt Quick drives the frame and swaps buffers after
		// recording beforeRenderPassRecording (game) + QML overlay.
		this->quick_window->update();
	}
	else if (this->context and this->context->get_raw_context()) {
		// Standalone renderer demos without a render callback draw directly
		// into the default framebuffer and swap manually.
		this->context->get_raw_context()->swapBuffers(this->quick_window.get());
	}
}


std::shared_ptr<Renderer> GlWindow::make_renderer() {
	ENSURE(this->context != nullptr,
	       "Cannot create GlRenderer before OpenGL context is initialized.");
	auto renderer = std::make_shared<GlRenderer>(this->get_context(),
	                                             this->size * this->scale_dpr);

	this->add_resize_callback([renderer](size_t w, size_t h, double scale) {
		// this up-scales all the default framebuffer to the "bigger" highdpi window.
		renderer->resize_display_target(w * scale, h * scale);
	});

	return renderer;
}


void GlWindow::make_context_current() {
	if (this->context and this->context->get_raw_context() and this->quick_window) {
		if (not this->context->get_raw_context()->makeCurrent(this->quick_window.get())) {
			log::log(MSG(warn) << "Failed to make QQuickWindow OpenGL context current");
		}
	}
}


void GlWindow::done_context_current() {
	if (this->context and this->context->get_raw_context()) {
		this->context->get_raw_context()->doneCurrent();
	}
}


const std::shared_ptr<opengl::GlContext> &GlWindow::get_context() const {
	return this->context;
}


const std::shared_ptr<QQuickWindow> &GlWindow::get_quick_window() const {
	return this->quick_window;
}


} // namespace openage::renderer::opengl
