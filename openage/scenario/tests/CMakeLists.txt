# openage scenario tests.
#
# Pure-Python pytest module. NOINSTALL because the engine runtime
# does not ship tests.
add_py_modules(
	__init__.py
	NOINSTALL test_p1_dump.py
)