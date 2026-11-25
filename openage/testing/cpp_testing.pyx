# Copyright 2015-2016 the openage authors. See copying.md for legal info.

"""
Provides a wrapper for openage::testing::run_method.
"""

from libopenage.testing.testlist cimport run_method as run_method
from libopenage.testing.testlist cimport run_snapshot_scenario


def run_cpp_method(str methodname):
    """ Runs the given C++ void () method, by its name. """
    run_method(methodname.encode())


def snapshot_scenario(str scenario):
    """Run the CPU-only C++ scenario and return canonical UTF-8 snapshots."""
    return [value.decode("utf-8") for value in run_snapshot_scenario(scenario.encode("utf-8"))]
