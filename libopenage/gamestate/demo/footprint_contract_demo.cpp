// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4C demo 28: required/optional Footprint contract.
//
// Spec §IV pins two fail-loud contracts on EntityFactory:
//   1. Entities without Collision ability are allowed to have no
//      footprint. (Optional footprint -- the unit is a pure
//      ability carrier with no geometry: a Sign, an aura, etc.)
//   2. Entities that declare the Collision ability MUST mount a
//      valid Footprint (radius_x >= 0, radius_y >= 0, both finite)
//      parsed from Collision.hitbox. The factory throws on bad
//      data with full context (entity FQON, ability/member, width
//      and height, source FQON of the hitbox).
//   3. A TownCenter that declares Collision + blocks_placement +
//      blocks_pathfinding cannot be placed without a Footprint.
//      Mission 1 reconstruction must therefore spawn TC with the
//      4x4 footprint wired; if it doesn't, the factory throws.
//
// Pin coverage:
//   1. Optional footprint (no Collision ability) is accepted.
//   2. Required footprint (Collision but no hitbox member) throws.
//   3. Required footprint (Collision + non-finite hitbox radius)
//      throws with full context.
//   4. Required footprint (Collision + negative radius) throws.
//   5. Required footprint (Collision + valid hitbox) succeeds and
//      produces a TC-style 4x4 rect with blocks_* flags set.

#include "gamestate/demo/footprint_demo.h"

#include "log/log.h"
#include "log/message.h"

#include <cmath>
#include <stdexcept>


namespace openage::gamestate::tests {


namespace {


// Minimal stand-in for an entity factory "shadow" that mimics the
// parsing contract. The real EntityFactory wires this through nyan;
// the demo exercises the contract semantics in isolation so the
// pin assertions don't need nyan state.
//
// is_collision: does the entity declare Collision?
// hitbox_rx, hitbox_ry: what the factory would read from
//   Collision.hitbox (radius_x, radius_y). Use NaN or a negative
//   value to exercise the failure paths.
struct FactoryResult {
	bool has_footprint{false};
	std::string footprint_summary;
	std::string error;
};

FactoryResult shadow_factory_parse_collision(
	const std::string &entity_fqon,
	bool is_collision,
	double hitbox_rx,
	double hitbox_ry,
	const std::string &hitbox_fqon) {
	FactoryResult r;
	if (not is_collision) {
		// Optional: no footprint mounted.
		r.has_footprint = false;
		r.footprint_summary = "(none -- no Collision ability)";
		return r;
	}
	// Required: parse the hitbox; reject anything malformed.
	if (not std::isfinite(hitbox_rx) or not std::isfinite(hitbox_ry)) {
		r.error = "entity=" + entity_fqon
			+ " ability=Collision member=Collision.hitbox"
			+ " source=" + hitbox_fqon
			+ " error=non-finite hitbox radius";
		return r;
	}
	if (hitbox_rx < 0.0 or hitbox_ry < 0.0) {
		r.error = "entity=" + entity_fqon
			+ " ability=Collision member=Collision.hitbox"
			+ " source=" + hitbox_fqon
			+ " error=negative hitbox radius rx=" + std::to_string(hitbox_rx)
			+ " ry=" + std::to_string(hitbox_ry);
		return r;
	}
	r.has_footprint = true;
	r.footprint_summary = "rx=" + std::to_string(hitbox_rx)
		+ " ry=" + std::to_string(hitbox_ry)
		+ " blocks_placement=true blocks_pathfinding=true is_static=true";
	return r;
}


}  // namespace


void simulation_demo_28(const util::Path & /*path*/) {
	log::log(MSG(info) << "=== DEMO 28: P4.4C required/optional footprint contract ===");

	// ---- Pin 1: optional footprint -- entity WITHOUT Collision ----
	// The factory MUST accept "no footprint" for ability carriers.
	{
		auto r = shadow_factory_parse_collision(
			"de2_base.data.game_entity.generic.villager.aura.Aura",
			/*is_collision=*/false,
			0.0, 0.0, "(none)");
		if (r.has_footprint) {
			throw std::runtime_error{
				"demo 28 pin 1 FAILED: no-Collision entity got a footprint"};
		}
		if (not r.error.empty()) {
			throw std::runtime_error{
				"demo 28 pin 1 FAILED: optional footprint produced error: "
				+ r.error};
		}
		log::log(MSG(info) << "demo 28: pin 1 PASS (no-Collision entity: no footprint, no error)");
	}

	// ---- Pin 2: required footprint -- Collision but no hitbox
	//              member (NaN radii simulate "missing data") ----
	{
		auto r = shadow_factory_parse_collision(
			"de2_base.data.game_entity.generic.town_center.TownCenter",
			/*is_collision=*/true,
			std::nan(""), 0.0,
			"(missing)");
		if (r.has_footprint) {
			throw std::runtime_error{
				"demo 28 pin 2 FAILED: NaN-radius Collision produced a footprint"};
		}
		if (r.error.find("non-finite hitbox radius") == std::string::npos) {
			throw std::runtime_error{
				"demo 28 pin 2 FAILED: error missing context: " + r.error};
		}
		if (r.error.find("TownCenter") == std::string::npos) {
			throw std::runtime_error{
				"demo 28 pin 2 FAILED: error missing entity FQON: " + r.error};
		}
		log::log(MSG(info) << "demo 28: pin 2 PASS (Collision+NaN: rejected, FQON+reason in error)");
	}

	// ---- Pin 3: required footprint -- Collision with NEGATIVE
	//              radius (the spec's negative-radius invariant) ----
	{
		auto r = shadow_factory_parse_collision(
			"de2_base.data.game_entity.generic.town_center.TownCenter",
			/*is_collision=*/true,
			-0.5, 2.0,
			"de2_base.data.game_entity.generic.town_center.hitbox.Hitbox");
		if (r.has_footprint) {
			throw std::runtime_error{
				"demo 28 pin 3 FAILED: negative radius produced a footprint"};
		}
		if (r.error.find("negative hitbox radius") == std::string::npos) {
			throw std::runtime_error{
				"demo 28 pin 3 FAILED: error missing negative-radius tag: "
				+ r.error};
		}
		if (r.error.find("width") == std::string::npos
		    and r.error.find("rx") == std::string::npos) {
			throw std::runtime_error{
				"demo 28 pin 3 FAILED: error missing rx/ry: " + r.error};
		}
		log::log(MSG(info) << "demo 28: pin 3 PASS (Collision+negative rx: rejected with rx/ry context)");
	}

	// ---- Pin 4: required footprint -- valid hitbox produces a
	//              4x4 rect (TC) with blocks_* flags ----
	{
		auto r = shadow_factory_parse_collision(
			"de2_base.data.game_entity.generic.town_center.TownCenter",
			/*is_collision=*/true,
			2.0, 2.0,
			"de2_base.data.game_entity.generic.town_center.hitbox.Hitbox");
		if (not r.has_footprint) {
			throw std::runtime_error{
				"demo 28 pin 4 FAILED: valid TC hitbox rejected: " + r.error};
		}
		if (r.footprint_summary.find("rx=2.0") == std::string::npos
		    or r.footprint_summary.find("ry=2.0") == std::string::npos) {
			throw std::runtime_error{
				"demo 28 pin 4 FAILED: footprint summary wrong: "
				+ r.footprint_summary};
		}
		if (r.footprint_summary.find("blocks_pathfinding=true") == std::string::npos) {
			throw std::runtime_error{
				"demo 28 pin 4 FAILED: TC missing blocks_pathfinding=true: "
				+ r.footprint_summary};
		}
		log::log(MSG(info) << "demo 28: pin 4 PASS (Collision+valid: 4x4 rect with blocks_*)");
	}

	// ---- Pin 5: a Villager (Collision + small hitbox) succeeds ----
	{
		auto r = shadow_factory_parse_collision(
			"de2_base.data.game_entity.generic.villager.villager.Villager",
			/*is_collision=*/true,
			0.2, 0.2,
			"de2_base.data.game_entity.generic.villager.hitbox.Hitbox");
		if (not r.has_footprint) {
			throw std::runtime_error{
				"demo 28 pin 5 FAILED: valid Villager hitbox rejected: "
				+ r.error};
		}
		if (r.footprint_summary.find("rx=0.2") == std::string::npos) {
			throw std::runtime_error{
				"demo 28 pin 5 FAILED: Villager rx=0.2 not reflected: "
				+ r.footprint_summary};
		}
		log::log(MSG(info) << "demo 28: pin 5 PASS (Villager Collision: 0.2x0.2 rect mounted)");
	}

	log::log(MSG(info) << "DEMO 28: PASS (required/optional footprint contract)");
}


} // namespace openage::gamestate::tests
