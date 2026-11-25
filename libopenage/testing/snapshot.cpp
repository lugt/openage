// Copyright 2026+ the openage authors. See copying.md for legal info.

module;

#include <exception>
#include <string>
#include <vector>

#include "error/error.h"
#include "log/message.h"
#include "renderer/render_pass.h"

export module openage.testing.snapshot;

import openage.gamestate.command_source;
import openage.gamestate.state_snapshot;
import openage.renderer.recording;

export namespace openage::testing::snapshot {

/** Run 100 samples and an independent 50/save/restore/50 run without devices or assets. */
std::vector<std::string> run_scenario(const std::string &scenario);

} // namespace openage::testing::snapshot

namespace openage::testing::snapshot {

namespace {
std::string record_frame(const gamestate::headless::HeadlessWorld &world) {
	renderer::RecordingRenderer renderer;
	auto pass = renderer.add_render_pass({}, nullptr);
	auto quad = renderer.add_bufferless_quad();
	for (const auto &[id, entity] : world.get_entities()) {
		renderer::RecordingCommand sprite{
			renderer::RecordingCommand::Kind::SPRITE,
			entity.unit_type + "_" + std::to_string(id),
			entity.x_centi,
			entity.y_centi,
			2};
		renderer::RecordingCommand hp{
			renderer::RecordingCommand::Kind::UI,
			"hp_" + std::to_string(id),
			0,
			0};
		hp.rect = {entity.x_centi / 100 - 5, entity.y_centi / 100 - 14, 20, 4};
		hp.fill = entity.hp_centi * 100 / entity.max_hp_centi;
		// Submit HUD first to exercise layer ordering rather than insertion order.
		pass->add_renderables({std::make_shared<renderer::RecordingUniformInput>(hp), quad}, 20);
		pass->add_renderables({std::make_shared<renderer::RecordingUniformInput>(sprite), quad}, 10);
	}
	renderer.begin_frame();
	renderer.render(pass);
	return renderer.get_text();
}
} // namespace

std::vector<std::string> run_scenario(const std::string &scenario) try {
	using namespace gamestate::headless;
	auto source = JsonScenarioCommandSource::from_json_text(scenario);
	auto complete = HeadlessWorld::from_scenario(source.get_scenario());
	complete.step_ticks(100, source);
	std::string checkpoint;
	{
		auto interrupted = HeadlessWorld::from_scenario(source.get_scenario());
		interrupted.step_ticks(50, source);
		checkpoint = interrupted.serialize();
	}
	// Construct a fresh command source too: commands are selected by absolute tick.
	auto resumed_source = JsonScenarioCommandSource::from_json_text(scenario);
	auto restored = HeadlessWorld::deserialize(checkpoint);
	auto roundtrip = restored.serialize();
	restored.step_ticks(50, resumed_source);
	return {complete.serialize(), restored.serialize(), record_frame(complete), record_frame(restored), checkpoint, roundtrip};
}
catch (const std::exception &exc) {
	throw Error{MSG(err) << "Snapshot scenario failed: " << exc.what()};
}

} // namespace openage::testing::snapshot
