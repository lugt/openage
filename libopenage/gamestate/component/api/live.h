// Copyright 2021-2025 the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <nyan/nyan.h>

#include "curve/container/map.h"
#include "gamestate/component/api_component.h"
#include "gamestate/component/types.h"
#include "time/time.h"


namespace openage::gamestate::component {
class Live final : public APIComponent {
public:
	using APIComponent::APIComponent;

	component_t get_type() const override;

	/**
	 * Add a new attribute to the component attributes.
	 *
	 * @param time The time at which the attribute is added.
	 * @param attribute Attribute identifier (fqon of the nyan object).
	 * @param starting_values Attribute values at the time of addition.
	 */
	void add_attribute(const time::time_t &time,
	                   const nyan::fqon_t &attribute,
	                   std::shared_ptr<curve::Discrete<int64_t>> starting_values);

	/**
	 * Set the value of an attribute at a given time.
	 *
	 * Triggers any callback registered via \p on_attribute_change for
	 * this attribute once the new value lands.
	 *
	 * @param time The time at which the attribute is set.
	 * @param attribute Attribute identifier (fqon of the nyan object).
	 * @param value New attribute value.
	 */
	void set_attribute(const time::time_t &time,
	                   const nyan::fqon_t &attribute,
	                   int64_t value);

	/**
	 * Read the current value of an attribute at a given time.
	 *
	 * Used by combat systems to subtract damage from Health. The hit path
	 * reads the current value via this method and writes the post-damage
	 * value with \p set_attribute, which also fires the change callbacks
	 * (e.g. the Health<=0 despawn hook registered in Phase C4).
	 *
	 * @param time The time at which the attribute is queried.
	 * @param attribute Attribute identifier (fqon of the nyan object).
	 * @return The attribute value at \p time, or std::nullopt if the
	 *         attribute is not registered on this entity.
	 */
	std::optional<int64_t> get_attribute_value(const time::time_t &time,
	                                           const nyan::fqon_t &attribute) const;

	/**
	 * Remove an attribute from storage.
	 *
	 * Used when an entity's HP hits zero (the engine chooses to forget
	 * the curve entry rather than letting it keep ticking down to a
	 * sentinel, mirroring how AoE2's corpse cleanup runs). No-op if the
	 * attribute is not present. Registered callbacks for this attribute
	 * are discarded along with the data - subscribe again if the entity
	 * is re-spawned with a fresh Live.
	 *
	 * @param time The time at which the attribute is removed.
	 * @param attribute Attribute identifier (fqon of the nyan object).
	 */
	void remove_attribute(const time::time_t &time,
	                      const nyan::fqon_t &attribute);

	/**
	 * Register a callback that fires whenever this attribute's value
	 * changes via \p set_attribute.
	 *
	 * The callback receives the new (post-write) value; subscribers are
	 * responsible for any threshold logic (e.g. "if new_value <= 0
	 * despawn the entity"). Multiple callbacks per attribute are
	 * supported and fire in registration order. Callbacks are not
	 * cleared across set_attribute calls - the only way to clear them
	 * is to call \p remove_attribute or assign a new Live.
	 *
	 * Phase C4 will register a despawn callback on Health at entity
	 * creation; Combat systems will register damage logs on the same
	 * attribute for the tutorial HUD.
	 *
	 * @param attribute Attribute identifier (fqon of the nyan object).
	 * @param callback Callable invoked after each successful write.
	 */
	using attribute_change_callback_t = std::function<void(int64_t new_value)>;
	void on_attribute_change(const nyan::fqon_t &attribute,
	                         attribute_change_callback_t callback);

	/**
	 * Register a callback fired when the named attribute's post-write
	 * value drops to <= threshold for the first time. Idempotent: the
	 * callback fires AT MOST ONCE per attribute per Live instance, even
	 * if subsequent writes also fall below threshold. A subsequent
	 * write that takes the value back ABOVE the threshold does NOT
	 * re-arm the listener (the "one shot per Live" rule is permanent).
	 *
	 * Used by the death system (P3.3) to derive a DeathEvent from an
	 * HP<=0 transition. The callback is fired inside set_attribute()
	 * after a successful write so the listener sees the post-write
	 * value (matching the on_attribute_change semantics).
	 *
	 * The threshold direction is fixed at "drop below" (the typical
	 * death / resource-drained / objective-failed pattern); future
	 * "rise above" listeners can be added without breaking this API.
	 *
	 * @param attribute Attribute identifier (FQON of the nyan object).
	 * @param threshold Threshold value; the callback fires when the
	 *                  post-write value <= threshold.
	 * @param callback Callable invoked once after the triggering write.
	 */
	using threshold_callback_t = std::function<void(int64_t new_value)>;
	void on_threshold_drop(const nyan::fqon_t &attribute,
	                       int64_t threshold,
	                       threshold_callback_t callback);

private:
	using attribute_storage_t = curve::UnorderedMap<nyan::fqon_t,
	                                                std::shared_ptr<curve::Discrete<int64_t>>>;

	/**
	 * Map of attribute values by attribute type.
	 */
	attribute_storage_t attribute_values;

	/**
	 * Attribute-change callback registry. The attribute FQON identifies
	 * which attribute to watch; the vector holds the callbacks in
	 * registration order. We intentionally do not own the attribute
	 * curves from here - the callback is fired after a set_attribute
	 * write succeeds, with the new value handed back to the callback.
	 */
	std::unordered_map<nyan::fqon_t, std::vector<attribute_change_callback_t>> change_callbacks;

	/**
	 * Threshold listeners keyed by attribute FQON. Each entry stores
	 * (threshold, callback) tuples plus an "armed" flag. A listener
	 * is armed at registration; the first post-write value that drops
	 * to <= threshold fires the callback and permanently disarms it.
	 *
	 * Idempotency: even if the same FQON is registered twice (e.g.
	 * two death systems subscribe), the per-Live one-shot rule means
	 * the callback fires at most once.
	 */
	struct threshold_listener_t {
		int64_t threshold;
		threshold_callback_t callback;
		bool armed{true};
	};
	std::unordered_map<nyan::fqon_t, std::vector<threshold_listener_t>> threshold_listeners;
};

} // namespace openage::gamestate::component
