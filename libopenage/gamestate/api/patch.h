// Copyright 2023-2024 the openage authors. See copying.md for legal info.

#pragma once

#include <nyan/nyan.h>

#include "gamestate/api/types.h"


namespace openage::gamestate::api {

/**
 * Helper class for API patches.
 */
class APIPatch {
public:
	/**
	 * Check if a nyan object is a patch (type == \p engine.util.patch.Patch).
	 *
	 * @param obj nyan object handle.
	 *
	 * @return true if the object is a patch, else false.
	 */
	static bool is_patch(const nyan::Object &obj);

	/**
	 * Check if a patch has a given property.
	 *
	 * @param patch \p Patch nyan object (type == \p engine.util.patch.Patch).
	 * @param property Property type.
	 *
	 * @return true if the patch has the property, else false.
	 */
	static bool check_property(const nyan::Object &patch,
	                           const patch_property_t &property);

	/**
	 * Get the nyan object for a property from a patch.
	 *
	 * @param patch \p Patch nyan object (type == \p engine.util.patch.Patch).
	 * @param property Property type.
	 *
	 * @return \p Property nyan object (type == \p engine.util.patch.property.PatchProperty).
	 */
	static const nyan::Object get_property(const nyan::Object &patch,
	                                       const patch_property_t &property);

	/**
	 * Get the target FQON of a patch.
	 *
	 * A patch encodes a modification to a specific target object in the
	 * nyan database. The target is set when the patch is registered with
	 * nyan (via the parent of `Patch.patch` in nyan schema, i.e.
	 * `children(engine.util.patch.NyanPatch)`): each NyanPatch subtype
	 * carries a target FQON. This accessor surfaces that FQON so
	 * gameplay-side code can decide whether to apply the patch
	 * (Transaction::add) or pass it through (e.g. to a non-default
	 * sub-target via add(patch, target), which nyan does not implement
	 * yet).
	 *
	 * Thin wrapper around nyan::Object::get_target(), which itself
	 * derives the target from the patch's NyanPatch parents.
	 *
	 * @param patch \p Patch nyan object (type == \p engine.util.patch.Patch).
	 *
	 * @return Pointer to the target FQON string, or nullptr if the nyan
	 *         library cannot resolve a target (which is treated as an
	 *         authoring error - see Transaction::add).
	 */
	static const nyan::fqon_t *get_target(const nyan::Object &patch);
};
} // namespace openage::gamestate::api
