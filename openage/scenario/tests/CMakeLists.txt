# openage scenario tests.
#
# Pure-Python pytest module. NOINSTALL because the engine runtime
# does not ship tests.
add_py_modules(
	__init__.py
	NOINSTALL test_p1_dump.py
	NOINSTALL test_p2_ir.py
	NOINSTALL test_p2_validate.py
	NOINSTALL test_p2_writer.py
	NOINSTALL test_p2_loader.py
	NOINSTALL test_p2_terrain.py
	NOINSTALL test_p2_integration.py
)