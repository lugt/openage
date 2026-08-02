// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.1.1 negative test: empty FQON at a nyan lookup site.
//
// This demo runs the require_nonempty_fqon() helper against a
// handful of plausible empty-FQON call sites and asserts that
// the helper throws (release) and the error message contains the
// diagnostic context (handler_id, event_type, simulation_time,
// entity_id, entity_fqon, field_name, requested_fqon). It is
// the C6 negative-test gate that prevents the P3 production
// crash from regressing silently.

#include "gamestate/nyan_contracts.h"

#include "log/log.h"
#include "log/message.h"
#include "util/path.h"

#include <string>
#include <vector>


namespace openage::gamestate::tests {


namespace {


struct NyanContractExpectation {
	std::string name;
	std::string fqon;
	std::string ctx;
	std::string handler_id;
	std::string event_type;
	double simulation_time;
	int64_t entity_id;
	std::string entity_fqon;
	std::string field_name;
	// Substrings the thrown message MUST contain. We check a few
	// stable substrings; a full-string check would be too
	// brittle.
	std::vector<std::string> must_contain;
};


// Runs one negative case. If the helper accepts the input (no
// throw) the demo fails. If the helper throws but the message
// does not contain all the expected substrings the demo also
// fails.
void run_one(const NyanContractExpectation &c) {
	try {
		require_nonempty_fqon(
			c.fqon, c.ctx, c.handler_id, c.event_type,
			c.simulation_time, c.entity_id, c.entity_fqon, c.field_name);
		log::log(MSG(err) << "nyan_contract_negative[" << c.name
		                   << "]: helper accepted the empty FQON -- "
		                   << "it MUST throw NyanContractError");
		throw std::runtime_error{
			"require_nonempty_fqon accepted an empty FQON"};
	}
	catch (const NyanContractError &e) {
		std::string msg = e.what();
		for (auto &needle : c.must_contain) {
			if (msg.find(needle) == std::string::npos) {
				log::log(MSG(err) << "nyan_contract_negative[" << c.name
				                   << "]: message missing needle '"
				                   << needle << "': " << msg);
				throw std::runtime_error{
					"nyan_contract_negative: missing diagnostic"};
			}
		}
		log::log(MSG(info) << "nyan_contract_negative[" << c.name
		                    << "]: OK (caught, all needles present)");
	}
}


}  // namespace


void simulation_demo_15(const util::Path & /* path */) {
	log::log(MSG(info) << "=== DEMO 15: P4.1.1 nyan contract negative test ===");

	// Case 1: bare empty FQON, no context. Must throw, must
	// mention the call site and the empty requested_fqon.
	run_one({
		.name = "bare_empty",
		.fqon = "",
		.ctx = "unit::damage_lookup",
		.must_contain = {"empty FQON", "unit::damage_lookup",
		                 "requested_fqon=(empty)"},
	});

	// Case 2: empty FQON with full runtime context (handler,
	// event, time, entity, field). All of them must be present
	// in the message.
	run_one({
		.name = "full_context",
		.fqon = "",
		.ctx = "RemovalEventHandler::invoke",
		.handler_id = "game.removal",
		.event_type = "RemovalEvent",
		.simulation_time = 42.5,
		.entity_id = 7,
		.entity_fqon = "de2_base.data.game_entity.generic.villager.villager.Villager",
		.field_name = "FlatAttributeChange.change_value",
		.must_contain = {
			"game.removal", "RemovalEvent", "t=42.5",
			"entity_id=7", "Villager",
			"FlatAttributeChange.change_value", "empty FQON"},
	});

	// Case 3: non-empty FQON must NOT throw. The helper is
	// permissive: it only complains about empty input.
	try {
		require_nonempty_fqon("de2_base.attribute.Health", "ok_site");
		log::log(MSG(info) << "nyan_contract_negative[non_empty_accepts]: OK");
	}
	catch (...) {
		log::log(MSG(err) << "nyan_contract_negative: helper rejected a "
		                   << "non-empty FQON");
		throw;
	}

	log::log(MSG(info) << "DEMO 15: PASS (empty FQON detected + diagnosed, "
	                    << "non-empty FQON accepted)");
}


}  // namespace openage::gamestate::tests
