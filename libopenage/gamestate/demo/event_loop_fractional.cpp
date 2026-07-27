// Copyright 2026 the openage authors. See copying.md for legal info.
//
// EventLoop fractional-time characterisation test (Phase H0.5).
//
// What this is
// ------------
// A minimum characterisation of `EventLoop`'s scheduled-event semantics
// against the new peek_next_event_time() read API. The test pins down
// the three invariants the H0.5 manual-time driver relies on:
//
//   1. peek_next_event_time() returns the scheduled time of the
//      front element of the queue (next event dispatchable), without
//      popping, reordering, or scheduling anything.
//   2. When the headless driver advances by scheduled event time, the
//      first reach_time call lands exactly on that time -- it does not
//      overshoot to a higher time-unit (so a t=1.5 event is fired from
//      t=0, not from t=2.0).
//   3. reach_time(t) drains ALL pending events with time <= t in a
//      single call (it is not "drain one event then return"). After
//      draining a same-time batch, peek must report the next distinct
//      event time, not nullopt. Time-ordered batches are dispatched
//      monotonically (a later-time event is never invoked before an
//      earlier-time event).
//
// What this is NOT
// ----------------
// Not a replacement for the production EventLoop runner; the
// production loop continues to use the same execute_events semantics
// it has since before H0.5. peek is a side-effect-free read; the
// production loop does not adopt it.
//
// NOTE on insertion order of same-time events: the PairingHeap-backed
// EventStore orders same-time events by SharedPtrLess<Event> (a
// pointer-address fallback), which is NOT stable across runs. We do
// NOT assert a specific dispatch order for events scheduled at the
// same time -- only that all of them are invoked at exactly the
// scheduled time, and that later-time events are not invoked before
// earlier-time ones.

#include "demo_0.h"

#include <string>
#include <vector>

#include "event/event.h"
#include "event/event_loop.h"
#include "event/evententity.h"
#include "event/eventhandler.h"
#include "event/state.h"
#include "log/log.h"
#include "time/time.h"


namespace openage::gamestate::tests {

namespace {

// Minimal EventEntity shim so create_event() can take a non-nullptr
// target. The recorder handler keeps the time it was invoked at in
// the order events were dispatched to its invoke(). That is enough to
// pin down both the exact dispatch time for a t=1.5 scheduled event
// and the stable insertion order of same-time events.
class StubEntity : public openage::event::EventEntity {
public:
	StubEntity(const std::shared_ptr<openage::event::EventLoop> &loop)
		:
		openage::event::EventEntity(loop) {}
	size_t id() const override {
		return 1;
	}
	std::string idstr() const override {
		return "stub-entity";
	}
};

// Noop recorder handler. Stores the time it was invoked at, in
// invocation order. Lets the test assert stable ordering of same-time
// events and exact dispatch time for a t=1.5 scheduled event.
class RecordHandler : public openage::event::EventHandler {
public:
	RecordHandler(std::string name) :
		openage::event::EventHandler(name,
		                           openage::event::EventHandler::trigger_type::ONCE) {}
	~RecordHandler() override = default;

	std::vector<openage::time::time_t> recorded;

	void setup_event(const std::shared_ptr<openage::event::Event> & /*event*/,
	                const std::shared_ptr<openage::event::State> & /*state*/) override {
		// Noop: this handler does not register any state dependencies.
	}

	void invoke(openage::event::EventLoop & /*loop*/,
	            const std::shared_ptr<openage::event::EventEntity> & /*target*/,
	            const std::shared_ptr<openage::event::State> & /*state*/,
	            const openage::time::time_t &time,
	            const param_map & /*params*/) override {
		this->recorded.push_back(time);
	}

	openage::time::time_t predict_invoke_time(
		const std::shared_ptr<openage::event::EventEntity> & /*target*/,
		const std::shared_ptr<openage::event::State> & /*state*/,
		const openage::time::time_t &at) override {
		// Noop handler: the event should fire exactly when the caller
		// scheduled it. Returning TIME_MIN here would cause EventQueue
		// to silently drop the event (see EventQueue::create_event),
		// which is exactly what this test exists to detect.
		return at;
	}
};

bool fail_msg(const std::string &msg) {
	log::log(MSG(err) << "EventLoop fractional test: " << msg);
	return false;
}

} // namespace

bool event_loop_fractional_test(const util::Path & /*path*/) {
	auto loop = std::make_shared<openage::event::EventLoop>();
	auto state = std::make_shared<openage::event::State>(loop);
	auto stub_target = std::make_shared<StubEntity>(loop);

	// Schedule one event at t=1.5, then ask the loop what it thinks is
	// the next dispatch time. Must be exactly 1.5 (no drift to a higher
	// unit, no fractional collapse to 1.0).
	auto handler = std::make_shared<RecordHandler>("record-A");
	loop->add_event_handler(handler);
	loop->create_event(handler,
	                    stub_target,
	                    state,
	                    openage::time::time_t(1.5),
	                    /*params*/ {});

	// ---- Invariant 1: peek returns the head-of-queue time ----
	auto peeked = loop->peek_next_event_time();
	if (not peeked.has_value()) {
		return fail_msg("peek_next_event_time() returned nullopt on a "
		                "queue with one pending t=1.5 event");
	}
	auto peeked_value = peeked.value();
	if (peeked_value.to_float() != 1.5f) {
		return fail_msg("peek returned t=" + std::to_string(peeked_value.to_float())
		                + " (expected 1.5)");
	}

	// ---- Invariant 2: headless advance hits exactly 1.5 first ----
	loop->reach_time(peeked_value, state);
	if (handler->recorded.size() != 1) {
		return fail_msg("after reach_time(1.5), handler recorded "
		                + std::to_string(handler->recorded.size())
		                + " invocations (expected 1)");
	}
	if (handler->recorded.front().to_float() != 1.5f) {
		return fail_msg("handler invoked at t="
		                + std::to_string(handler->recorded.front().to_float())
		                + " (expected 1.5)");
	}

	// ---- Invariant 3: same-time batching + time-ordered drain ----
	// Schedule three events at exactly t=1.5 plus one tail event at
	// t=2.5. This exercises two semantics of the EventLoop the
	// H0.5 manual-time driver relies on:
	//
	//   (a) reach_time(t) drains ALL pending events with time <= t in
	//       a single call (it is not "drain one event then return").
	//       So the three t=1.5 events must all be invoked by ONE
	//       reach_time(1.5) call, with the t=2.5 tail still queued.
	//
	//   (b) After the t=1.5 batch drains, peek_next_event_time() must
	//       report 2.5 (not nullopt, not 1.5) and reach_time(2.5)
	//       must dispatch the tail at exactly t=2.5.
	//
	// NOTE on insertion ordering of same-time events: the
	// PairingHeap-backed EventStore orders same-time events by
	// SharedPtrLess<Event> (a pointer-address fallback), which is
	// NOT stable across runs. We therefore do NOT assert a specific
	// invocation order for the three t=1.5 events. We only assert
	// (1) all three are invoked exactly once, (2) all three observe
	// invoke-time == 1.5 exactly, and (3) the tail event is not
	// invoked until its scheduled time.
	std::vector<std::shared_ptr<RecordHandler>> more;
	more.push_back(std::make_shared<RecordHandler>("record-B"));
	more.push_back(std::make_shared<RecordHandler>("record-C"));
	more.push_back(std::make_shared<RecordHandler>("record-D"));
	auto tail = std::make_shared<RecordHandler>("record-T");
	for (auto &h : more) {
		loop->add_event_handler(h);
	}
	loop->add_event_handler(tail);

	loop->create_event(more[0], stub_target, state,
	                   openage::time::time_t(1.5), {});
	loop->create_event(more[1], stub_target, state,
	                   openage::time::time_t(1.5), {});
	loop->create_event(more[2], stub_target, state,
	                   openage::time::time_t(1.5), {});
	loop->create_event(tail, stub_target, state,
	                   openage::time::time_t(2.5), {});

	// Step (a): peek must report 1.5 (the next event time).
	auto next0 = loop->peek_next_event_time();
	if (not next0.has_value() or next0.value().to_float() != 1.5f) {
		return fail_msg("peek returned non-1.5 before draining the "
		                "3 same-time batch (got "
		                + std::to_string(next0.value_or(openage::time::time_t::from_int(-1)).to_float())
		                + ")");
	}

	// Step (b): reach_time(1.5) must drain all three t=1.5 events
	// (and must NOT touch the t=2.5 tail).
	loop->reach_time(next0.value(), state);
	for (size_t i = 0; i < more.size(); ++i) {
		if (more[i]->recorded.size() != 1) {
			return fail_msg("more[" + std::to_string(i) + "] recorded "
			                + std::to_string(more[i]->recorded.size())
			                + " invocations after reach_time(1.5) "
			                "(expected 1)");
		}
		if (more[i]->recorded.front().to_float() != 1.5f) {
			return fail_msg("more[" + std::to_string(i) + "] invoked at "
			                + std::to_string(more[i]->recorded.front().to_float())
			                + " (expected 1.5)");
		}
	}
	if (tail->recorded.size() != 0) {
		return fail_msg("tail event invoked before its scheduled "
		                "time of t=2.5 (recorded "
		                + std::to_string(tail->recorded.size()) + ")");
	}

	// Step (c): peek must now report 2.5 (not 1.5, not nullopt).
	auto next1 = loop->peek_next_event_time();
	if (not next1.has_value() or next1.value().to_float() != 2.5f) {
		return fail_msg("peek returned "
		                + std::to_string(next1.value_or(openage::time::time_t::from_int(-1)).to_float())
		                + " after draining the t=1.5 batch (expected 2.5)");
	}
	loop->reach_time(next1.value(), state);
	if (tail->recorded.size() != 1) {
		return fail_msg("tail event recorded "
		                + std::to_string(tail->recorded.size())
		                + " invocations after reach_time(2.5) "
		                "(expected 1)");
	}
	if (tail->recorded.front().to_float() != 2.5f) {
		return fail_msg("tail event invoked at "
		                + std::to_string(tail->recorded.front().to_float())
		                + " (expected 2.5)");
	}

	// After draining everything, peek must report the queue is empty.
	auto after = loop->peek_next_event_time();
	if (after.has_value()) {
		return fail_msg("after dispatching all events, peek still "
		                "returned t="
		                + std::to_string(after.value().to_float()));
	}

	log::log(MSG(info) << "EventLoop fractional test: PASS"
	                       " (peek hit 1.5 exactly, 3 same-time events "
	                       "batched in one reach_time(1.5), tail at "
	                       "t=2.5 dispatched in order, queue reported "
	                       "empty after dispatch)");
	return true;
}

void simulation_demo_8(const util::Path &path) {
	log::log(MSG(info) << "========== EVENTLOOP FRACTIONAL TIME TEST (demo 8) ==========");
	bool ok = event_loop_fractional_test(path);
	if (not ok) {
		log::log(MSG(err) << "EVENTLOOP FRACTIONAL TIME TEST: FAIL");
		return;
	}
}

} // namespace openage::gamestate::tests
