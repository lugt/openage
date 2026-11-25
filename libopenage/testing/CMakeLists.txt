add_sources(libopenage
	testing.cpp
	benchmark_test.cpp
)

pxdgen(
	testlist.h
)

add_sources(libopenage MODULE
	snapshot.cpp
)
