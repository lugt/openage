// Copyright 2014-2017 the openage authors. See copying.md for legal info.

#include "testing.h"

#include "testlist.h"

import openage.presenter.tests;
import openage.gamestate.movement_tests;
import openage.testing.snapshot;
import openage.gamestate.score;
import openage.gamestate.ai;

namespace openage {
namespace testing {


bool fail(const log::message &msg) {
	throw TestError{msg};
}

void campaign_scoring() {
	gamestate::tests::campaign_scoring();
}

void ai_action_limits() {
	gamestate::ai::tests::action_limits();
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


void presenter_without_scenario() {
	presenter::tests::without_scenario();
}


std::vector<std::string> run_snapshot_scenario(const std::string &scenario) {
	return snapshot::run_scenario(scenario);
}

}} // namespace openage::testing
