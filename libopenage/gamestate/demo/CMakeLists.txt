add_sources(libopenage
    attack_demo.cpp
    demo_0.cpp
	move_demo.cpp
	tests.cpp
	ww_mission1_regression.cpp
	ww_mission1_native_load.cpp
	ww_mission1_lifecycle.cpp
	damage_effect_path.cpp
	generic_death.cpp
	event_loop_fractional.cpp
)

pxdgen(
	tests.h
)
