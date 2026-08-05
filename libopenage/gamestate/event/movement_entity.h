// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4C: MovementEventTarget -- the EventEntity used as the
// target for the WaypointReachedEvent and MovementArrivedEvent
// dispatched from system::Move. The Move runtime has access to
// the GameState (and through it to entity ids) but NOT to any
// production EventEntity (Commander / Spawner live on the
// GameSimulation). The EventLoop's Event constructor requires
// a non-null EventEntity (it hashes entity->id()), so Move
// constructs a fresh MovementEventTarget per dispatch with a
// stable id derived from the moved entity's id.
//
// The target is intentionally a separate object from the
// entity: event observers reading entity_id from params should
// trust params, not target->id(). MovementEventTarget exists
// only so the Event constructor's hash + EventQueue bookkeeping
// have a valid EventEntity to bind to.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "event/evententity.h"
#include "event/event_loop.h"


namespace openage::gamestate::event {


class MovementEventTarget : public openage::event::EventEntity {
public:
	MovementEventTarget(
		const std::shared_ptr<openage::event::EventLoop> &loop,
		size_t moved_entity_id) :
		openage::event::EventEntity{loop},
		moved_entity_id_{moved_entity_id} {}

	size_t id() const override {
		return moved_entity_id_;
	}

	std::string idstr() const override {
		return "MovementEventTarget[entity=" + std::to_string(moved_entity_id_) + "]";
	}

private:
	size_t moved_entity_id_;
};


} // namespace openage::gamestate::event
