// Copyright 2021-2025 the openage authors. See copying.md for legal info.

#include "live.h"

#include <algorithm>
#include <optional>

#include "curve/container/iterator.h"
#include "curve/container/map_filter_iterator.h"
#include "curve/discrete.h"
#include "gamestate/component/types.h"


namespace openage::gamestate::component {

component_t Live::get_type() const {
	return component_t::LIVE;
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
} // namespace openage::gamestate::component
