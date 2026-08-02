// Copyright 2017-2023 the openage authors. See copying.md for legal info.

#include "event.h"

#include <atomic>
#include <compare>
#include <functional>
#include <string>

#include "log/log.h"
#include "log/message.h"

#include "event/evententity.h"
#include "event/eventhandler.h"
#include "time/time.h"
#include "util/fixed_point.h"
#include "util/hash.h"


namespace openage::event {


// P4.1.1: process-wide monotonic counter used as the tie-breaker
// in Event::operator< for events that match on (time, handler_id,
// entity_fqon). Reset to 0 at process start; no cross-process
// meaning. std::atomic to keep the constructor safe even if the
// event loop is later extended to multi-threaded dispatch.
std::atomic<uint64_t> Event::next_creation_seq{0};


Event::Event(const std::shared_ptr<EventEntity> &entity,
             const std::shared_ptr<EventHandler> &eventhandler,
             const EventHandler::param_map &params) :
	params(params),
	entity{entity},
	eventhandler{eventhandler},
	myhash{
		util::hash_combine(std::hash<size_t>()(entity->id()),
                           std::hash<std::string>()(eventhandler->id()))},
	creation_seq{Event::next_creation_seq.fetch_add(
		1, std::memory_order_relaxed)} {
}


void Event::depend_on(const std::shared_ptr<EventEntity> &dependency) {
	// TODO: do REPEAT and TRIGGER listen to changes (i.e. have dependents)?
	// if not, exclude them here and return early.

	log::log(DBG << "Registering dependency event from EventHandler "
		             << this->get_eventhandler()->id()
		             << " to EventEntity " << dependency->idstr());

	dependency->add_dependent(this->shared_from_this());
}


void Event::cancel(const time::time_t reference_time) {
	log::log(DBG << "Canceling event from EventHandler "
		             << this->get_eventhandler()->id()
		             << " for time t=" << this->get_time());

	// remove the target which releases the event in the next loop iteration
	this->entity.reset();
	this->set_last_changed(reference_time);
}


bool Event::operator<(const Event &other) const {
	// P4.1.1: strict weak ordering over the stable key
	// (time, handler_id, entity_fqon, creation_seq). See event.h
	// for the rationale.

	// 1. Time dominates.
	if (this->time != other.time) {
		return this->time < other.time;
	}

	// 2. Handler id (canonical name of the event handler that
	// owns the event; never empty).
	const std::string &this_handler = this->eventhandler->id();
	const std::string &other_handler = other.eventhandler->id();
	if (this_handler != other_handler) {
		return this_handler < other_handler;
	}

	// 3. Entity fqon (may be empty for handler-level events;
	// the handler is a stronger signal so we have already
	// broken the tie on handler when possible; here we need a
	// deterministic tie-breaker for events that match on
	// handler).
	auto this_entity = this->entity.lock();
	auto other_entity = other.entity.lock();
	std::string this_fqon;
	std::string other_fqon;
	if (this_entity) {
		this_fqon = this_entity->idstr();
	}
	if (other_entity) {
		other_fqon = other_entity->idstr();
	}
	if (this_fqon != other_fqon) {
		return this_fqon < other_fqon;
	}

	// 4. Creation sequence (monotonic counter assigned at
	// construction; independent of pointer address or hash
	// randomization).
	if (this->creation_seq != other.creation_seq) {
		return this->creation_seq < other.creation_seq;
	}

	// Truly equal: return false (strict weak ordering allows
	// equivalence classes). We never want the comparator to
	// return true for both a<b and b<a.
	return false;
}

} // namespace openage::event
