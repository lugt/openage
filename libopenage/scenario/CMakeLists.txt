# Mission Pack loader (P2.7).

# The reader is pure data; the loader wires the typed struct into the
# production GameState. Both live in libopenage/scenario so the rest
# of the engine (gamestate/, presenter/, renderer/) can depend on
# them without circular deps on openage/convert.

add_sources(libopenage
	mission_pack.cpp
	mission_pack_loader.cpp
	placement_validator.cpp
	static_occupancy_grid.cpp
	dynamic_occupancy.cpp
)

add_subdirectory("trigger")

pxdgen(
	mission_pack.h
	mission_pack_loader.h
)