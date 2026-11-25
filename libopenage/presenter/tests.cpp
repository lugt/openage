// Copyright 2026+ the openage authors. See copying.md for legal info.

module;

#include <filesystem>
#include <memory>
#include <string>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#include "gamestate/simulation.h"
#include "gamestate/save_load.h"
#include "presenter/hud_qml_adapter.h"
#include "presenter/viewmodel/selection_model.h"
#include "renderer/window_event_handler.h"
#include "rng/rng.h"
#include "testing/testing.h"
#include "time/time_loop.h"
#include "util/fslike/directory.h"

export module openage.presenter.tests;

export namespace openage::presenter::tests {

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
