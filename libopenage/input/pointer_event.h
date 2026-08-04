// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4D.2: PointerEvent -- the platform-agnostic semantic input event
// that the gameplay input layer consumes.
//
// The Qt/QtQuick layer is responsible for converting raw platform
// events (QMouseEvent, QTouchEvent, ...) into PointerEvents. Once
// a PointerEvent is in flight, NOTHING in the simulation /
// presenter / gameplay runtime may depend on QObject, QMouseEvent,
// or QML types. The gameplay input layer consumes ONLY:
//   - button
//   - action
//   - logical + backing coordinates (Qt maps to logical; macOS
//     device-pixel ratio maps to backing; both are platform-neutral
//     float fields)
//   - modifier bits
//
// Pipeline contract:
//   raw QEvent
//   -> Qt input manager (existing input::Event)
//   -> presenter's input adapter
//   -> PointerEvent (this header)
//   -> semantic action resolver (P4.4D.4)
//   -> Commander
//   -> MoveCommand / AttackCommand
//   -> Move runtime / Attack runtime
//   -> Simulation events
//   -> Renderer / HUD
//
// The Coordinate and conversion fields let a headless test (e.g.
// demo 33) drive the input layer without ever touching Qt: tests
// fill in (button, action, logical_x, logical_y, ...) and the
// presenter-side code converts logical -> backing -> world via
// coord::picking::screen_to_world_on_terrain and the test camera
// matrix.

#pragma once

#include <cstdint>


namespace openage::input {


/**
 * Mouse button (logical). The mapping from Qt::MouseButton is
 * done in the Qt adapter; the gameplay layer only sees these
 * platform-neutral values.
 */
enum class PointerButton : uint8_t {
	NONE = 0,
	LEFT,
	RIGHT,
	MIDDLE,
	BACK,
	FORWARD,
};


/**
 * Pointer action (what the user just did with the button).
 * Maps from QEvent::Type in the Qt adapter.
 */
enum class PointerAction : uint8_t {
	NONE = 0,
	PRESS,    // button-down transition
	RELEASE,  // button-up transition
	MOVE,     // movement while a button is held (drag)
	DOUBLE_CLICK,
	WHEEL,
};


/**
 * Keyboard modifier bits. The mapping from Qt::KeyboardModifier
 * is done in the Qt adapter; the gameplay layer only sees these
 * platform-neutral values.
 */
enum class PointerModifier : uint8_t {
	NONE = 0x0,
	SHIFT = 0x1,
	CONTROL = 0x2,
	ALT = 0x4,
	META = 0x8,
};


inline PointerModifier operator|(PointerModifier a, PointerModifier b) {
	return static_cast<PointerModifier>(
		static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline PointerModifier &operator|=(PointerModifier &a, PointerModifier b) {
	a = a | b;
	return a;
}

inline bool has_modifier(PointerModifier mods, PointerModifier test) {
	return (static_cast<uint8_t>(mods) & static_cast<uint8_t>(test))
	       == static_cast<uint8_t>(test);
}


/**
 * Platform-agnostic pointer event. The Qt adapter fills this in
 * once; every downstream consumer (semantic action resolver,
 * Commander, demo tests) sees only this struct.
 *
 * Coordinate semantics:
 *   - logical_x/y: pixel coordinates in the OS / Qt logical pixel
 *     space (DPR-scaled; matches the unit coord::input uses).
 *   - backing_x/y: pixel coordinates in the renderer's backing
 *     pixel space (physical pixels; matches the unit
 *     util::Vector2s viewport_size uses on macOS / Retina).
 *     On a 1.0 DPR display logical == backing; on Retina they
 *     differ by the device pixel ratio. Tests can leave backing
 *     == logical when DPR is 1.0.
 */
struct PointerEvent {
	PointerButton button{PointerButton::NONE};
	PointerAction action{PointerAction::NONE};
	PointerModifier mods{PointerModifier::NONE};

	float logical_x{0.0f};
	float logical_y{0.0f};
	float backing_x{0.0f};
	float backing_y{0.0f};

	// Viewport size in the same unit as the coordinates above.
	// Used by the semantic action layer when converting
	// PointerEvent -> world coords via coord::picking. The
	// presenter-side input adapter fills this from the camera's
	// viewport (which already distinguishes logical from
	// backing via viewport_size() vs scaled_viewport_size()).
	uint32_t viewport_width{0};
	uint32_t viewport_height{0};
	float device_pixel_ratio{1.0f};
};


} // namespace openage::input
