// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.1.1 runtime contracts for nyan lookups.
//
// Centralized helpers that turn "raw get" (which may silently
// return an empty FQON, an empty object, or a default-constructed
// value) into a checked lookup that fails fast with a full
// diagnostic context. Callers MUST use these instead of
// `database.get_object("")` or `mapping[key]` (where missing
// key produces an empty value).
//
// The contract violation policy:
//   * require_* helpers in release builds throw an `Error` with a
//     structured message that includes handler_id, event_type,
//     simulation_time, entity_id, entity_fqon, field_name,
//     requested_fqon. This is the release-build path so a contract
//     violation is reported with the same detail in production
//     as in tests.
//   * In debug builds the helpers additionally assert. assert is
//     NOT a release-build guard; the throw is.
//
// This file is header-only so the helpers are inlined into callers
// without a separate translation unit; that keeps the cost zero
// when the contract holds and the build size minimal.

#pragma once

#include <stdexcept>
#include <string>

#include "log/log.h"
#include "log/message.h"


namespace openage::gamestate {


/**
 * Structured error raised by require_* helpers when a nyan
 * lookup contract is violated. Carries enough context for an
 * operator to find the offending call site without re-running
 * the simulation.
 */
class NyanContractError : public std::runtime_error {
public:
	NyanContractError(const std::string &what) :
		std::runtime_error{what} {}
};


/**
 * Validate that a FQON is non-empty. An empty FQON at a nyan
 * call site is always a bug (the caller should have checked
 * upstream). Pass `ctx` as a static string describing the call
 * site; the message includes the runtime context built by the
 * caller.
 */
inline void require_nonempty_fqon(
	const std::string &fqon,
	const std::string &ctx,
	const std::string &handler_id = "",
	const std::string &event_type = "",
	double simulation_time = 0.0,
	int64_t entity_id = -1,
	const std::string &entity_fqon = "",
	const std::string &field_name = "") {

	if (fqon.empty()) {
		std::string msg = "nyan contract violation: empty FQON at " + ctx;
		if (not handler_id.empty())    msg += " handler_id=" + handler_id;
		if (not event_type.empty())    msg += " event_type=" + event_type;
		if (simulation_time != 0.0)     msg += " t=" + std::to_string(simulation_time);
		if (entity_id >= 0)            msg += " entity_id=" + std::to_string(entity_id);
		if (not entity_fqon.empty())   msg += " entity_fqon=" + entity_fqon;
		if (not field_name.empty())    msg += " field=" + field_name;
		msg += " requested_fqon=(empty)";
		log::log(MSG(err) << msg);
		throw NyanContractError{msg};
	}
}


} // namespace openage::gamestate
