# Copyright 2015-2024 the openage authors. See copying.md for legal info.

from cpython.ref cimport PyObject
from libcpp.string cimport string
from libcpp.vector cimport vector

from libopenage.main cimport main_arguments, run_game as run_game_cpp
from libopenage.util.path cimport Path as Path_cpp
from libopenage.pyinterface.pyobject cimport PyObj
from libopenage.error.handlers cimport set_exit_ok


def run_game(args, root_path):
    """
    Launches the game after arguments were translated.
    """
    cdef int result

    # argument translation
    cdef main_arguments args_cpp

    set_exit_ok(False)
    try:
        # root_path is a util.fslike.Path object from python
        args_cpp.root_path = Path_cpp(
            root_path.subpath,
            PyObj(<PyObject*>root_path.fsobj),
        )

        # opengl debugging
        args_cpp.gl_debug = args.gl_debug

        # headless mode
        args_cpp.headless = args.headless

        # scenario mode + FQON strings. ``args.tutorial`` is the bool flag (was --tutorial pre-migration);
        # the FQONs default to empty strings if not set on the Python side (was tutorial_player_setup_fqon pre-migration).
        # Encoding as utf-8 because ``main_arguments`` carries std::string.
        args_cpp.scenario_mode = bool(getattr(args, "scenario_mode", False))
        args_cpp.scenario_player_setup_fqon = \
            getattr(args, "scenario_player_setup_fqon", "").encode('utf-8')
        args_cpp.scenario_fqon = \
            getattr(args, "scenario_fqon", "").encode('utf-8')

        args_cpp.ai_config = getattr(args, "ai_config_json", "").encode("utf-8")

        # mods
        if args.modpacks is not None:
            args_cpp.mods = args.modpacks
        else:
            args_cpp.mods = vector[string]()

        # window
        args_cpp.window_args.width = args.window_args["width"]
        args_cpp.window_args.height = args.window_args["height"]
        args_cpp.window_args.vsync = args.window_args["vsync"]
        args_cpp.window_args.ignore_user_input = getattr(args, "ignore_user_input", False)
        args_cpp.window_args.mode = args.window_args["window_mode"].encode('utf-8')

        # run the game!
        with nogil:
            result = run_game_cpp(args_cpp)

        return result
    finally:
        set_exit_ok(True)
