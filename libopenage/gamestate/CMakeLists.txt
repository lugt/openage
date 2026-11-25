add_sources(libopenage
	chat_console.cpp
    definitions.cpp
    entity_factory.cpp
	game_entity.cpp
    game_state.cpp
	game.cpp
	manager.cpp
    map.cpp
	player.cpp
	save_load.cpp
    simulation.cpp
	terrain_chunk.cpp
    terrain_factory.cpp
    terrain_tile.cpp
	terrain.cpp
	types.cpp
	world.cpp
	universe.cpp
)

add_sources(libopenage MODULE
	ai.cpp
	command_source.cpp
	movement_tests.cpp
	score.cpp
	state_snapshot.cpp
)

add_subdirectory(activity/)
add_subdirectory(api/)
add_subdirectory(component/)
add_subdirectory(demo/)
add_subdirectory(event/)
add_subdirectory(system/)
add_subdirectory(objective/)
