# openage scenario python package.
#
# This package contains the Mission 1 Scenario IR loader + dump
# validator used by the P1.4 tests and the future P2/P4 work. It is
# pure Python (no Cython bindings) and ships NOINSTALL because the
# engine runtime does not need it; it is consumed only by `pytest`
# and developer-side verifiers.
add_py_modules(
	__init__.py
	NOINSTALL dumps.py
)
add_subdirectory(tests)