// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include "util/path.h"


namespace openage::gamestate::tests {


/**
 * Demo 24 - P4.4A HudQmlAdapter headless contract.
 *
 * Builds the SelectionModel + HudQmlAdapter, then drives the same
 * events as demo 23 and asserts the adapter's Q_PROPERTY values
 * track the simulation. QML binds to these Q_PROPERTYs in
 * production; the headless test pins the contract so a regression
 * in either the ViewModel or the adapter is caught.
 */
void simulation_demo_24(const util::Path &path);


} // namespace openage::gamestate::tests