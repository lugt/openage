// Copyright 2014-2017 the openage authors. See copying.md for legal info.

#include "testing.h"

#include "testlist.h"

import openage.presenter.tests;
import openage.renderer.gui.guisys.livereload_tests;
import openage.gamestate.movement_tests;
import openage.testing.snapshot;
import openage.gamestate.score;
import openage.gamestate.ai;
import openage.gamestate.wildlife;

namespace openage {
namespace testing {


bool fail(const log::message &msg) {
	throw TestError{msg};
}

void gui_source_changes() {
	qtgui::tests::source_changes();
}

void hud_clicks() {
	presenter::tests::hud_clicks();
}

void hud_native_clicks() {
	presenter::tests::hud_clicks(true);
}

void gui_native_input() {
	qtgui::tests::native_input();
}

void campaign_scoring() {
	gamestate::tests::campaign_scoring();
}

void ai_action_limits() {
	gamestate::ai::tests::action_limits();
}

void wildlife_neighbors() {
	gamestate::wildlife::tests::neighbors();
}

void wildlife_behavior() {
	gamestate::wildlife::tests::behavior();
}

void wildlife_spatial() {
	gamestate::wildlife::tests::spatial_benchmark();
}

void scoring() {
	gamestate::score::tests::scoring();
}

void movement_replanning() {
	gamestate::tests::movement_replanning();
}


void automation_input() {
	presenter::tests::automation_input();
}


void input_edge_cases() {
	presenter::tests::input_edge_cases();
}


void presenter_without_scenario() {
	presenter::tests::without_scenario();
}


std::vector<std::string> run_snapshot_scenario(const std::string &scenario) {
	return snapshot::run_scenario(scenario);
}

}} // namespace openage::testing
