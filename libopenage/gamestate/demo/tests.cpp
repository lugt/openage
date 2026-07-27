// Copyright 2023-2023 the openage authors. See copying.md for legal info.

#include "tests.h"

#include "log/log.h"
#include "log/message.h"

#include "gamestate/demo/attack_demo.h"
#include "gamestate/demo/demo_0.h"
#include "gamestate/demo/move_demo.h"


namespace openage::gamestate::tests {

void simulation_demo(int demo_id, const util::Path &path) {
	switch (demo_id) {
	case 0:
		simulation_demo_0(path);
		break;

	case 1:
		simulation_demo_1(path);
		break;

	case 2:
		simulation_demo_2(path);
		break;

	case 3:
		simulation_demo_3(path);
		break;

	case 4:
		simulation_demo_4(path);
		break;

	case 5:
		simulation_demo_5(path);
		break;

	case 6:
		simulation_demo_6(path);
		break;

	case 7:
		// Phase H0: WW Mission 1 headless regression (manual time).
		simulation_demo_7(path);
		break;

	default:
		log::log(MSG(err) << "Unknown renderer demo requested: " << demo_id << ".");
		break;
	}
}

} // namespace openage::gamestate::tests
