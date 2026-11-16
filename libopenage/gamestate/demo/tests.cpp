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
#include "gamestate/demo/selection_events_demo.h"
#include "gamestate/demo/selection_optional_component_demo.h"
#include "gamestate/demo/production_context_input_demo.h"
#include "gamestate/demo/hud_adapter_demo.h"
#include "gamestate/demo/footprint_demo.h"


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

	case 23:
		// Phase P4.4A: SelectionModel event wiring.
		simulation_demo_23(path);
		break;

	case 24:
		// Phase P4.4A: HudQmlAdapter Q_PROPERTY contract.
		simulation_demo_24(path);
		break;

	case 25:
		// Phase P4.4B: Footprint construction contract.
		simulation_demo_25(path);
		break;

	case 26:
		// Phase P4.4B: PlacementValidator contract.
		simulation_demo_26(path);
		break;

	case 27:
		// Phase P4.4B: StaticOccupancyGrid contract.
		simulation_demo_27(path);
		break;

	case 28:
		// Phase P4.4C: required/optional Footprint contract.
		simulation_demo_28(path);
		break;

	case 29:
		// Phase P4.4C: GridAStarPathfinder contract.
		simulation_demo_29(path);
		break;

	case 30:
		// Phase P4.4C: mover footprint clearance.
		simulation_demo_30(path);
		break;

	case 31:
		// Phase P4.4C: segmented movement + waypoint events.
		simulation_demo_31(path);
		break;

	case 32:
		// Phase P4.4C: Mission 1 TC obstacle routing.
		simulation_demo_32(path);
		break;

	case 35:
		// Phase P4.4D: ContextActionResolver contract.
		simulation_demo_35(path);
		break;

	case 36:
		// Phase P4.4D: windowed-style move routing.
		simulation_demo_36(path);
		break;

	case 37:
		// Phase P4.4D.1: AttackApproach service contract.
		simulation_demo_37(path);
		break;

	case 38:
		// Phase P4.4D.1: pointer-driven Mission 1 combat lifecycle.
		simulation_demo_38(path);
		break;

	case 42:
		// Phase P4.4F.1: swept AABB + DynamicOccupancy.
		simulation_demo_42(path);
		break;

	case 46:
		// Phase P4.4D.2 + P4.4F: multi-unit formation +
		// dynamic occupancy.
		simulation_demo_46(path);
		break;

	case 47:
		// Phase P4.4F.1: MovementReservation + narrow passage.
		simulation_demo_47(path);
		break;

	case 48:
		// Phase P4.4F.1: group move via GroupMoveHandler.
		simulation_demo_48(path);
		break;

	case 49:
		// Phase P4.4F.1: real multi-unit no-overlap runtime.
		simulation_demo_49(path);
		break;

	case 50:
		// Regression: selecting an entity without optional components
		// (no Attack) must not throw.
		simulation_demo_50(path);
		break;

	case 51:
		// Regression: production RMB binding must pick entities and
		// distinguish ATTACK from MOVE.
		simulation_demo_51(path);
		break;

	default:
		log::log(MSG(err) << "Unknown renderer demo requested: " << demo_id << ".");
		break;
	}
}

} // namespace openage::gamestate::tests
