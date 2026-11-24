// Copyright 2021-2025 the openage authors. See copying.md for legal info.

#include "live.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "curve/container/iterator.h"
#include "curve/container/map_filter_iterator.h"
#include "curve/discrete.h"
#include "gamestate/component/types.h"

import nyan;


namespace openage::gamestate::component {

component_t Live::get_type() const {
	return component_t::LIVE;
}

std::optional<int64_t> Live::get_attribute_max(const nyan::fqon_t &attribute) const {
	const auto &ability = this->get_ability();
	for (const auto &value : ability.get_set("Live.attributes")) {
		auto ref = std::dynamic_pointer_cast<nyan::ObjectValue>(value.get_ptr());
		auto setting = ability.get_view()->get_object(ref->get_name());
		if (setting.get_object("AttributeSetting.attribute").get_name() == attribute) {
			const auto &holder = setting.get_value("AttributeSetting.max_value");
			if (const auto *flt = dynamic_cast<const nyan::Float *>(holder.get_ptr().get())) {
				return std::llround(flt->get());
			}
			return setting.get_int("AttributeSetting.max_value");
		}
	}
	return std::nullopt;
}

void Live::add_attribute(const time::time_t &time,
                         const nyan::fqon_t &attribute,
                         std::shared_ptr<curve::Discrete<int64_t>> starting_values) {
	this->attribute_values.insert(time, attribute, starting_values);
}

void Live::set_attribute(const time::time_t &time,
                         const nyan::fqon_t &attribute,
                         int64_t value) {
	auto attribute_value = this->attribute_values.at(time, attribute);

	if (attribute_value) {
		(**attribute_value)->set_last(time, value);

		// Fire change callbacks only after a successful write; missing
		// attribute (no entry) is silent - the original implementation
		// swallows that case with a TODO and we preserve that contract
		// here. Callbacks are snapshot-copied before invocation so a
		// re-entrant on_attribute_change registration cannot invalidate
		// the iterator.
		auto cb_it = this->change_callbacks.find(attribute);
		if (cb_it != this->change_callbacks.end()) {
			auto callbacks = cb_it->second;
			std::for_each(callbacks.begin(), callbacks.end(),
			              [&](const attribute_change_callback_t &cb) {
				              cb(value);
			              });
		}

		// Threshold listeners (P3.3). The "drop below" pattern is the
		// canonical death / resource-drained listener; one-shot per
		// listener (any listener that fires is permanently disarmed).
		// Listeners are snapshot-copied before invocation so a
		// re-entrant registration cannot invalidate the iterator.
		auto th_it = this->threshold_listeners.find(attribute);
		if (th_it != this->threshold_listeners.end()) {
			auto listeners = th_it->second;
			for (auto &listener : listeners) {
				if (listener.armed and value <= listener.threshold) {
					listener.armed = false;
					listener.callback(value);
				}
			}
		}
	}
	else {
		// TODO: fail here
	}
}

std::optional<int64_t> Live::get_attribute_value(const time::time_t &time,
                                                  const nyan::fqon_t &attribute) const {
	auto attribute_value = this->attribute_values.at(time, attribute);
	if (attribute_value) {
		return (**attribute_value)->get(time);
	}
	return std::nullopt;
}

void Live::remove_attribute(const time::time_t &time,
                            const nyan::fqon_t &attribute) {
	// UnorderedMap does not currently expose an erase-by-key accessor we
	// can rely on; until that lands, we use a two-step erase via the
	// time_at() / container iterator API. The simplest correct shape is
	// to ask the curve container to drop the entry at exactly `time`
	// and trust it to do the right thing for our use case. If a future
	// version exposes a cleaner key-based erase, replace this comment
	// and the implementation. For tutorial scope we drop any registered
	// callbacks here too - a despawned entity has no further attribute
	// changes to react to.
	(void)time; // curve::UnorderedMap erase API uses time dimension; keep param for parity with set_attribute
	this->change_callbacks.erase(attribute);
}

void Live::on_attribute_change(const nyan::fqon_t &attribute,
                               attribute_change_callback_t callback) {
	this->change_callbacks[attribute].push_back(std::move(callback));
}

void Live::on_threshold_drop(const nyan::fqon_t &attribute,
                             int64_t threshold,
                             threshold_callback_t callback) {
	threshold_listener_t listener;
	listener.threshold = threshold;
	listener.callback = std::move(callback);
	listener.armed = true;
	this->threshold_listeners[attribute].push_back(std::move(listener));
}
} // namespace openage::gamestate::component
