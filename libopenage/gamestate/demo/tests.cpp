// Copyright 2023-2023 the openage authors. See copying.md for legal info.

#include "tests.h"

#include "log/log.h"
#include "log/message.h"

#include "gamestate/demo/attack_demo.h"
#include "gamestate/demo/demo_0.h"
#include "gamestate/demo/move_demo.h"
#include "gamestate/demo/ww_mission1_native_load.h"
#include "gamestate/demo/ww_mission1_lifecycle.h"
#include "gamestate/demo/damage_effect_path.h"
#include "gamestate/demo/generic_death.h"
#include "gamestate/demo/deferred_removal.h"


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

	case 8:
		// Phase H0.5: EventLoop fractional-time characterisation.
		simulation_demo_8(path);
		break;

	case 9:
		// Phase P2.7: Mission Pack native integration.
		simulation_demo_9(path);
		break;

	case 10:
		// Phase P3.1: Mission 1 lifecycle characterization.
		simulation_demo_10(path);
		break;

	case 11:
		// Phase P3.2: Damage routed through ApplyDiscreteEffect handler.
		simulation_demo_11(path);
		break;

	case 12:
		// Phase P3.3: Generic death event.
		simulation_demo_12(path);
		break;

	case 13:
		// Phase P3.4: Deferred dead-entity removal.
		simulation_demo_13(path);
		break;

	default:
		log::log(MSG(err) << "Unknown renderer demo requested: " << demo_id << ".");
		break;
	}
}

} // namespace openage::gamestate::tests
