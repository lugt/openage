// Copyright 2026+ the openage authors. See copying.md for legal info.

module;

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QResizeEvent>
#include <QTemporaryDir>
#include <QTimer>
#include <QWheelEvent>

#include "config.h"
#include "gamestate/save_load.h"
#include "gamestate/simulation.h"
#include "input/controller/hud/binding_context.h"
#include "input/controller/hud/controller.h"
#include "input/event.h"
#include "presenter/hud_qml_adapter.h"
#include "presenter/viewmodel/selection_model.h"
#include "renderer/gui/guisys/public/gui_application.h"
#include "renderer/stages/hud/render_entity.h"
#include "renderer/window_event_handler.h"
#include "renderer/window.h"
#include "rng/rng.h"
#include "testing/testing.h"
#include "time/time_loop.h"
#include "util/fslike/directory.h"

export module openage.presenter.tests;

export namespace openage::presenter::tests {

/** Exercise shipped HUD pointer actions offscreen or in the native game window. */
void hud_clicks(bool native = false) {
	qtgui::GuiApplication application;
	QQuickRenderControl control;
	std::shared_ptr<renderer::Window> native_window;
	std::unique_ptr<QQuickWindow> offscreen_window;
	if (native) {
		renderer::window_settings settings;
		settings.width = 1280;
		settings.height = 720;
		native_window = renderer::Window::create("openage mission HUD regression", settings);
		native_window->set_render_callback([] {});
	}
	else {
		offscreen_window = std::make_unique<QQuickWindow>(&control);
	}
	auto *quick_window = native ? qobject_cast<QQuickWindow *>(native_window->get_qt_window().get()) : offscreen_window.get();
	TESTEQUALS(quick_window != nullptr, true);
	auto &window = *quick_window;
	QQmlEngine engine;
	HudQmlAdapter hud;
	QTemporaryDir saves;
	TESTEQUALS(saves.isValid(), true);
	hud.set_saves_dir(saves.path().toStdString());
	engine.rootContext()->setContextProperty("missionHud", &hud);
	QQmlComponent component{&engine, QUrl::fromLocalFile(
		QString::fromUtf8(config::buildsystem_sourcefile_dir) + "assets/qml/MissionHud.qml")};
	if (component.isError()) {
		TESTFAILMSG(component.errorString().toStdString());
	}
	std::unique_ptr<QObject> object{component.create()};
	auto *root = qobject_cast<QQuickItem *>(object.get());
	TESTEQUALS(root != nullptr, true);
	root->setParentItem(window.contentItem());
	int game_presses = 0;
	if (native_window) {
		native_window->add_mouse_button_callback([&](const QMouseEvent &event) {
			game_presses += event.type() == QEvent::MouseButtonPress;
		});
	}
	std::function<QQuickItem *(QQuickItem *, const QString &)> find_button;
	find_button = [&](QQuickItem *item, const QString &label) -> QQuickItem * {
		if (item->isVisible() and (item->property("label").toString() == label
		                         or item->property("fallbackText").toString() == label)) {
			return item;
		}
		for (auto *child : item->childItems()) {
			if (auto *found = find_button(child, label)) {
				return found;
			}
		}
		return nullptr;
	};
	auto polish = [&] {
		if (not native_window) {
			control.polishItems();
			return;
		}
		QEventLoop events;
		QTimer deadline;
		deadline.setSingleShot(true);
		bool rendered = false;
		const auto connection = QObject::connect(&window, &QQuickWindow::frameSwapped, &events, [&] {
			rendered = true;
			events.quit();
		});
		QObject::connect(&deadline, &QTimer::timeout, &events, &QEventLoop::quit);
		native_window->update();
		deadline.start(2000);
		events.exec();
		QObject::disconnect(connection);
		TESTEQUALS(rendered, true);
	};
	auto click = [&](const QString &label) {
		polish();
		auto *button = find_button(root, label);
		TESTEQUALS(button != nullptr, true);
		const auto point = button->mapToScene(QPointF{button->width() / 2, button->height() / 2});
		const int previous_game_presses = game_presses;
		const bool enabled = button->isEnabled();
		for (const auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
			QMouseEvent event{type, point, point, Qt::LeftButton,
			                 type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier};
			QCoreApplication::sendEvent(&window, &event);
		}
		if (native_window) {
			native_window->update();
			if (enabled) {
				TESTEQUALS(game_presses, previous_game_presses);
			}
		}
	};
	int stops = 0;
	QString requested;
	QObject::connect(&hud, &HudQmlAdapter::action_requested, [&requested](const QString &option, bool) {
		requested = option;
	});
	QObject::connect(&hud, &HudQmlAdapter::stop_requested, [&stops] {
		++stops;
	});
	for (const auto size : {QSize{1280, 720}, QSize{960, 640}}) {
		window.resize(size);
		root->setSize(size);
		hud.openMenu();
		click("Settings");
		TESTEQUALS(hud.menu_page().toStdString(), "settings");
		click("2.0x");
		TESTEQUALS(hud.game_speed(), 2.0);
		const bool units = hud.map_units();
		click(units ? "Minimap Units: ON" : "Minimap Units: OFF");
		TESTEQUALS(hud.map_units(), not units);
		click("Back");
		TESTEQUALS(hud.menu_page().toStdString(), "main");
		click("Save / Load Game");
		TESTEQUALS(hud.menu_page().toStdString(), "saveload");
		click("Back");
		click("Resume Game (Esc)");
		TESTEQUALS(hud.menu_open(), false);
		hud.set_selection_details({}, {}, 0, true, {});
		const int previous_stops = stops;
		click("Stop");
		TESTEQUALS(stops, previous_stops + 1);
		hud.set_selection_details({}, {}, 0, false, {});
		click("Stop");
		TESTEQUALS(stops, previous_stops + 1);

		const QVariantList cards{QVariantMap{{"id", 1}, {"name", "Villager"}}};
		const QVariantList actions{
			QVariantMap{{"id", "house"}, {"name", "House"}, {"build", true}, {"category", "economic"}},
			QVariantMap{{"id", "barracks"}, {"name", "Barracks"}, {"build", true}, {"category", "military"}},
		};
		hud.set_selection_details(cards, actions, 0, true, {});
		TESTEQUALS(hud.command_options().size(), 2);
		click("Ec");
		TESTEQUALS(hud.command_page().toStdString(), "economic");
		TESTEQUALS(hud.command_options().size(), 2);
		click("Ho");
		TESTEQUALS(requested.toStdString(), "house");
		// Health/resource refreshes must not kick the user out of a submenu.
		hud.set_selection_details(cards, actions, 1, true, {});
		TESTEQUALS(hud.command_page().toStdString(), "economic");
		click("Back");
		TESTEQUALS(hud.command_page().isEmpty(), true);
		TESTEQUALS(hud.activateCommand(1), true);
		TESTEQUALS(hud.command_page().toStdString(), "military");
		TESTEQUALS(hud.activateCommand(0), true);
		TESTEQUALS(requested.toStdString(), "barracks");
		hud.set_selection_details({QVariantMap{{"id", 2}, {"name", "Villager"}}}, actions, 0, true, {});
		TESTEQUALS(hud.command_page().isEmpty(), true);
		if (native_window) {
			hud.openChat();
			polish();
			auto *input = window.activeFocusItem();
			TESTEQUALS(input != nullptr, true);
			QKeyEvent typing{QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a"};
			QCoreApplication::sendEvent(&window, &typing);
			TESTEQUALS(input->property("text").toString().toStdString(), "a");
			QKeyEvent escape{QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier};
			QCoreApplication::sendEvent(&window, &escape);
			TESTEQUALS(hud.chat_open(), false);
		}
	}
}

/** Selection and HUD attachment must work without a scenario runtime or assets. */
void without_scenario() {
	auto temporary = std::filesystem::temp_directory_path() / ("openage-presenter-" + std::to_string(rng::random_seed()));
	std::filesystem::create_directories(temporary / "assets" / "converted");
	auto directory = std::make_shared<util::fslike::Directory>(temporary.string());
	auto root = directory->root();
	std::filesystem::create_directories(temporary / "cfg");
	TESTEQUALS(gamestate::GameSaveManager::directory(root), (temporary / "cfg" / "saves").string());
	TESTEQUALS(std::filesystem::exists(temporary / "cfg" / "saves"), false);
	auto simulation = std::make_shared<gamestate::GameSimulation>(
		root, nullptr, std::make_shared<time::TimeLoop>());
	TESTEQUALS(simulation->get_trigger_runtime(), nullptr);
	viewmodel::SelectionModel selection{simulation};
	HudQmlAdapter hud;
	hud.attach(selection, *simulation);
	selection.set_selected(std::nullopt);
	TESTEQUALS(hud.selected_entity_id(), -1);
	TESTEQUALS(hud.mission_is_victory(), false);
	TESTEQUALS(hud.mission_route().isEmpty(), true);
	TESTEQUALS(hud.mission_stockpile().isEmpty(), true);
	(root / "assets" / "converted").rmdir();
	(root / "assets").rmdir();
	(root / "cfg").rmdir();
	root.rmdir();
}

/** Automation must discard gameplay input, retain window events and let Escape close. */
void automation_input() {
	renderer::WindowEventHandler guarded{true};
	QKeyEvent key{QEvent::KeyPress, Qt::Key_A, Qt::NoModifier};
	QMouseEvent mouse{QEvent::MouseMove, QPointF{1, 2}, QPointF{1, 2}, Qt::NoButton, Qt::NoButton, Qt::NoModifier};
	QWheelEvent wheel{QPointF{1, 2}, QPointF{1, 2}, QPoint{}, QPoint{0, 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false};
	TESTEQUALS(guarded.eventFilter(nullptr, &key), true);
	TESTEQUALS(guarded.eventFilter(nullptr, &mouse), true);
	TESTEQUALS(guarded.eventFilter(nullptr, &wheel), true);
	TESTEQUALS(guarded.pop_events().empty(), true);

	QKeyEvent shortcut{QEvent::ShortcutOverride, Qt::Key_A, Qt::ControlModifier};
	shortcut.setAccepted(false);
	TESTEQUALS(guarded.eventFilter(nullptr, &shortcut), true);
	TESTEQUALS(shortcut.isAccepted(), true);
	QKeyEvent escape{QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier};
	TESTEQUALS(guarded.eventFilter(nullptr, &escape), true);
	auto events = guarded.pop_events();
	TESTEQUALS(events.size(), 1);
	TESTEQUALS(events.front()->type(), QEvent::Close);

	QResizeEvent resize{QSize{1280, 720}, QSize{1024, 768}};
	TESTEQUALS(guarded.eventFilter(nullptr, &resize), false);
	events = guarded.pop_events();
	TESTEQUALS(events.size(), 1);
	TESTEQUALS(events.front()->type(), QEvent::Resize);

	renderer::WindowEventHandler ordinary{false};
	TESTEQUALS(ordinary.eventFilter(nullptr, &key), false);
	TESTEQUALS(ordinary.eventFilter(nullptr, &mouse), false);
	TESTEQUALS(ordinary.pop_events().size(), 2);
}

/** Wheel/trackpad classification, modifier-key drag selection, and focus-loss queueing. */
void input_edge_cases() {
	// 1. Wheel & trackpad delta classification
	QWheelEvent wheel_up{QPointF{10, 10}, QPointF{10, 10}, QPoint{}, QPoint{0, 120},
	                     Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false};
	QWheelEvent wheel_down{QPointF{10, 10}, QPointF{10, 10}, QPoint{}, QPoint{0, -120},
	                       Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false};
	QWheelEvent wheel_horiz{QPointF{10, 10}, QPointF{10, 10}, QPoint{20, 0}, QPoint{120, 0},
	                        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false};
	QWheelEvent wheel_zero{QPointF{10, 10}, QPointF{10, 10}, QPoint{}, QPoint{},
	                       Qt::NoButton, Qt::NoModifier, Qt::ScrollEnd, false};
	QWheelEvent trackpad_up{QPointF{10, 10}, QPointF{10, 10}, QPoint{0, 15}, QPoint{},
	                        Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false};
	QWheelEvent trackpad_down{QPointF{10, 10}, QPointF{10, 10}, QPoint{0, -15}, QPoint{},
	                          Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false};

	TESTEQUALS(input::Event{wheel_up}.cc.code, 1);
	TESTEQUALS(input::Event{wheel_down}.cc.code, -1);
	TESTEQUALS(input::Event{wheel_horiz}.cc.code, 0);
	TESTEQUALS(input::Event{wheel_zero}.cc.code, 0);
	TESTEQUALS(input::Event{trackpad_up}.cc.code, 1);
	TESTEQUALS(input::Event{trackpad_down}.cc.code, -1);

	// 2. HUD drag controller must tolerate modifiers pressed before or mid-drag
	auto hud_ctrl = std::make_shared<input::hud::Controller>();
	auto hud_ctx = std::make_shared<input::hud::BindingContext>();
	input::hud::setup_defaults(hud_ctx, nullptr);

	QMouseEvent lmb_press{QEvent::MouseButtonPress, QPointF{100, 100}, QPointF{100, 100},
	                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
	QMouseEvent shift_move{QEvent::MouseMove, QPointF{220, 180}, QPointF{220, 180},
	                       Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier};
	QMouseEvent shift_release{QEvent::MouseButtonRelease, QPointF{220, 180}, QPointF{220, 180},
	                          Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier};

	TESTEQUALS(hud_ctrl->process(input::event_arguments{input::Event{lmb_press}, coord::input{100, 100}, coord::input_delta{0, 0}, {}}, hud_ctx), true);
	TESTEQUALS(hud_ctrl->get_drag_entity() != nullptr, true);
	TESTEQUALS(hud_ctrl->process(input::event_arguments{input::Event{shift_move}, coord::input{220, 180}, coord::input_delta{120, 80}, {}}, hud_ctx), true);
	TESTEQUALS(hud_ctrl->get_drag_entity()->is_changed(), true);
	TESTEQUALS(hud_ctrl->process(input::event_arguments{input::Event{shift_release}, coord::input{220, 180}, coord::input_delta{0, 0}, {}}, hud_ctx), true);
	TESTEQUALS(hud_ctrl->get_drag_entity() == nullptr, true);

	QMouseEvent ctrl_press{QEvent::MouseButtonPress, QPointF{50, 60}, QPointF{50, 60},
	                       Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier};
	QMouseEvent ctrl_release{QEvent::MouseButtonRelease, QPointF{90, 90}, QPointF{90, 90},
	                         Qt::LeftButton, Qt::NoButton, Qt::ControlModifier};
	TESTEQUALS(hud_ctrl->process(input::event_arguments{input::Event{ctrl_press}, coord::input{50, 60}, coord::input_delta{0, 0}, {}}, hud_ctx), true);
	TESTEQUALS(hud_ctrl->get_drag_entity() != nullptr, true);
	TESTEQUALS(hud_ctrl->process(input::event_arguments{input::Event{ctrl_release}, coord::input{90, 90}, coord::input_delta{0, 0}, {}}, hud_ctx), true);
	TESTEQUALS(hud_ctrl->get_drag_entity() == nullptr, true);

	// 3. WindowEventHandler must queue Leave, FocusOut, and WindowDeactivate
	renderer::WindowEventHandler handler{false, false};
	QEvent leave{QEvent::Leave};
	QFocusEvent focus_out{QEvent::FocusOut, Qt::ActiveWindowFocusReason};
	QEvent deactivate{QEvent::WindowDeactivate};
	TESTEQUALS(handler.eventFilter(nullptr, &leave), false);
	TESTEQUALS(handler.eventFilter(nullptr, &focus_out), false);
	TESTEQUALS(handler.eventFilter(nullptr, &deactivate), false);
	auto queued = handler.pop_events();
	TESTEQUALS(queued.size(), 3);
	TESTEQUALS(queued[0]->type(), QEvent::Leave);
	TESTEQUALS(queued[1]->type(), QEvent::FocusOut);
	TESTEQUALS(queued[2]->type(), QEvent::WindowDeactivate);
}

} // namespace openage::presenter::tests
