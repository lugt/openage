// Copyright 2026+ the openage authors. See copying.md for legal info.

module;

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include <QCoreApplication>
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
#include <QWheelEvent>

#include "config.h"
#include "gamestate/save_load.h"
#include "gamestate/simulation.h"
#include "presenter/hud_qml_adapter.h"
#include "presenter/viewmodel/selection_model.h"
#include "renderer/gui/guisys/public/gui_application.h"
#include "renderer/window_event_handler.h"
#include "rng/rng.h"
#include "testing/testing.h"
#include "time/time_loop.h"
#include "util/fslike/directory.h"

export module openage.presenter.tests;

export namespace openage::presenter::tests {

/** Exercise the shipped HUD with pointer events, without game assets or a GPU. */
void hud_clicks() {
	qtgui::GuiApplication application;
	QQuickRenderControl control;
	QQuickWindow window{&control};
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
	auto click = [&](const QString &label) {
		control.polishItems();
		auto *button = find_button(root, label);
		TESTEQUALS(button != nullptr, true);
		const auto point = button->mapToScene(QPointF{button->width() / 2, button->height() / 2});
		for (const auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
			QMouseEvent event{type, point, point, Qt::LeftButton,
			                 type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier};
			QCoreApplication::sendEvent(&window, &event);
		}
	};
	int stops = 0;
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

} // namespace openage::presenter::tests
