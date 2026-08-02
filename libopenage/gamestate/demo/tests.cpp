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
#include "gamestate/demo/event_driven_objective.h"
#include "gamestate/demo/nyan_contract_negative.h"
#include "gamestate/demo/trigger_condition_demo.h"
#include "gamestate/demo/trigger_evaluation_demo.h"
#include "gamestate/demo/trigger_delay_demo.h"
#include "gamestate/demo/trigger_effect_order_demo.h"
#include "gamestate/demo/trigger_data_driven_victory_demo.h"
#include "gamestate/demo/selected_view_demo.h"


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

	case 14:
		// Phase P4.1: Event-driven Mission 1 objective.
		simulation_demo_14(path);
		break;

	case 15:
		// Phase P4.1.1: nyan contract negative test (empty-FQON
		// contract gate; see gamestate/nyan_contracts.h).
		simulation_demo_15(path);
		break;

	case 16:
		// Phase P4.2: EntityRemovedCondition unit test.
		simulation_demo_16(path);
		break;

	case 17:
		// Phase P4.2: trigger evaluation single-shot dispatch.
		simulation_demo_17(path);
		break;

	case 18:
		// Phase P4.2: trigger delay (WAITING_DELAY state machine).
		simulation_demo_18(path);
		break;

	case 19:
		// Phase P4.2: multi-effect ordering (stable effect_index).
		simulation_demo_19(path);
		break;

	case 20:
		// Phase P4.3: Mission 1 data-driven victory (production path).
		simulation_demo_20(path);
		break;

	case 22:
		// Phase P4.4A: SelectedEntityView + MissionView contract.
		simulation_demo_22(path);
		break;

	default:
		log::log(MSG(err) << "Unknown renderer demo requested: " << demo_id << ".");
		break;
	}
}

} // namespace openage::gamestate::tests
