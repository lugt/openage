// Copyright 2015-2025 the openage authors. See copying.md for legal info.

#include "main.h"

#include <memory>

#include "engine/engine.h"
#include "util/timer.h"

namespace openage {

/*
 * This function is called from Cython, see main_cpp.pyx.
 *
 * This is the main entry point to the C++ part.
 */
int run_game(const main_arguments &args) {
	// TODO: store args.gl_debug as default in the cvar system.

	util::Timer timer;
	timer.start();

	// set engine run_mode
	openage::engine::Engine::mode run_mode = openage::engine::Engine::mode::FULL;
	if (args.headless) {
		run_mode = openage::engine::Engine::mode::HEADLESS;
	}

	// convert window arguments to window settings
	renderer::window_settings win_settings = {};
	win_settings.width = args.window_args.width;
	win_settings.height = args.window_args.height;
	win_settings.vsync = args.window_args.vsync;
	win_settings.ignore_user_input = args.window_args.ignore_user_input;

	renderer::window_mode wmode;
	if (args.window_args.mode == "fullscreen") {
		wmode = renderer::window_mode::FULLSCREEN;
	}
	else if (args.window_args.mode == "borderless") {
		wmode = renderer::window_mode::BORDERLESS;
	}
	else if (args.window_args.mode == "windowed") {
		wmode = renderer::window_mode::WINDOWED;
	}
	else {
		throw Error(MSG(err) << "Invalid window mode: " << args.window_args.mode);
	}
	win_settings.mode = wmode;
	win_settings.debug = args.gl_debug;

	openage::engine::Engine engine{run_mode,
	                               args.root_path,
	                               args.mods,
	                               win_settings,
	                               args.scenario_mode,
	                               args.scenario_player_setup_fqon,
	                               args.scenario_fqon};

	// Phase G1: explicit boundary between Engine construction and
	// loop(). After this returns, every worker-safe object (game,
	// scenario, objective controller, FULL presenter/renderer) has been
	// set up on the main thread. loop() may now spawn T1/T2 and (FULL)
	// run the presenter on the main thread without any "is_initialized"
	// guard, because initialize_runtime() has either succeeded (and
	// runtime_initialized=true) or thrown (no partial state to drive).
	engine.initialize_runtime();
	if (not args.ai_config.empty()) {
		engine.configure_ai(args.ai_config);
	}

	engine.loop();

	return 0;
}

} // namespace openage
