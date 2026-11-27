// Copyright 2015-2024 the openage authors. See copying.md for legal info.

#include "window.h"

#include <QWindow>

#include "opengl/window.h"
#include "window_event_handler.h"


namespace openage::renderer {

std::shared_ptr<Window> Window::create(const std::string &title,
                                       window_settings settings) {
	// currently we only have a functional GL window
	// TODO: support other renderer windows
	//       and add some selection mechanism.
	return std::make_shared<opengl::GlWindow>(title, settings);
}


Window::Window(size_t width, size_t height, bool ignore_user_input, bool queue_input_events) :
	size{width, height},
	event_handler{std::make_shared<WindowEventHandler>(ignore_user_input, queue_input_events)} {}


const util::Vector2s &Window::get_size() const {
	return this->size;
}

double Window::get_scale() const {
	return this->scale_dpr;
}

bool Window::should_close() const {
	return this->should_be_closed;
}

void Window::add_mouse_button_callback(const mouse_button_cb_t &cb) {
	this->on_mouse_button.push_back(cb);
}

void Window::add_mouse_move_callback(const mouse_move_cb_t &cb) {
	this->on_mouse_move.push_back(cb);
}

void Window::add_mouse_wheel_callback(const mouse_wheel_cb_t &cb) {
	this->on_mouse_wheel.push_back(cb);
}

void Window::add_key_callback(const key_cb_t &cb) {
	this->on_key.push_back(cb);
}

void Window::add_resize_callback(const resize_cb_t &cb) {
	this->on_resize.push_back(cb);
}

void Window::add_focus_lost_callback(const focus_lost_cb_t &cb) {
	this->on_focus_lost.push_back(cb);
}

void Window::set_render_callback(render_cb_t cb) {
	this->on_render = std::move(cb);
}

void Window::set_post_render_callback(render_cb_t cb) {
	this->on_post_render = std::move(cb);
}

const std::shared_ptr<QWindow> &Window::get_qt_window() const {
	return this->window;
}

void Window::close() {
	this->window->close();
}


} // namespace openage::renderer
