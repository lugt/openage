add_cython_modules(
	cpp_testing.pyx
	misc_cpp.pyx
)

add_py_modules(
	__init__.py
	doctest.py
	launcher.py
	list_processor.py
	main.py
	snapshot.py
	terrain.py
	testing.py
	testlist.py
	wallace_golden.py
	benchmark.py
)
