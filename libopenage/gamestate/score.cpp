// Copyright 2026+ the openage authors. See copying.md for legal info.

module;

#include <cstdint>
#include <string_view>

#include "gamestate/types.h"
#include "testing/testing.h"

export module openage.gamestate.score;

export namespace openage::gamestate::score {

/** Population capacity and other engine resources do not contribute to score. */
bool is_scored_resource(std::string_view resource) {
	return resource == "de2_base.data.util.resource.types.Wood"
	    or resource == "de2_base.data.util.resource.types.Food"
	    or resource == "de2_base.data.util.resource.types.Gold"
	    or resource == "de2_base.data.util.resource.types.Stone";
}

/** Original manual's resource-value formula, using integer arithmetic throughout. */
PlayerScore calculate(const ScoreLedger &ledger, int64_t stockpile, int64_t assets,
                      int64_t explored, int64_t tiles) {
	PlayerScore row;
	row.military = ledger.destroyed_value / 5;
	row.economy = (stockpile + 2 * assets) / 10;
	row.technology = ledger.research_value / 5 + (tiles > 0 ? 1000 * explored / tiles : 0);
	row.total = row.military + row.economy + row.technology;
	return row;
}

namespace tests {

/** Spending, losses and kills change distinct terms; exploration is bounded by map size. */
void scoring() {
	const auto initial = calculate({}, 500, 100, 0, 100);
	TESTEQUALS(initial.total, 70);
	TESTEQUALS(calculate({}, 400, 200, 0, 100).total, 80);
	TESTEQUALS(calculate({}, 400, 100, 0, 100).total, 60);
	const auto fought = calculate({100, 50}, 400, 100, 25, 100);
	TESTEQUALS(fought.military, 20);
	TESTEQUALS(fought.technology, 260);
	TESTEQUALS(fought.total, 340);
	TESTEQUALS(calculate({}, 0, 0, 100, 100).total, 1000);
	TESTEQUALS(calculate({}, 0, 0, 0, 0).total, 0);
	TESTEQUALS(calculate({4, 4}, 9, 0, 1, 3).total, 333);
	TESTEQUALS(is_scored_resource("de2_base.data.util.resource.types.PopulationSpace"), false);
}

} // namespace tests
} // namespace openage::gamestate::score
