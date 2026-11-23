# openage scenario python package.
#
# This package contains the Mission 1 Scenario IR loader + dump
# validator + writer + native-loader bridge used by the P1.4 tests
# and the P2.x work. It is pure Python (no Cython bindings) and
# ships NOINSTALL because the engine runtime does not need it; it is
# consumed only by `pytest` and developer-side verifiers.
add_py_modules(
	__init__.py
	NOINSTALL dumps.py
	NOINSTALL de2_campaign.py
	NOINSTALL de2_scenario.py
	NOINSTALL de2_save_map.py
	NOINSTALL ir.py
	NOINSTALL loader.py
	NOINSTALL terrain.py
	NOINSTALL validation.py
	NOINSTALL writer.py
)
add_subdirectory(tests)