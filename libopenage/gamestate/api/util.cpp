// Copyright 2023-2023 the openage authors. See copying.md for legal info.

#include "util.h"

#include <cmath>
#include <regex>
#include <vector>

#include "error/error.h"
#include "log/message.h"

import nyan;


namespace openage::gamestate::api {

int64_t get_nyan_int_or_float(const nyan::Object &obj, const char *member) {
	const auto &holder = obj.get_value(member);
	if (const auto *flt = dynamic_cast<const nyan::Float *>(holder.get_ptr().get())) {
		return std::llround(flt->get());
	}
	return obj.get_int(member);
}

int64_t population_cost(const std::shared_ptr<nyan::View> &view, const std::string &subject) {
	int64_t population = 0;
	for (const auto &entry : view->get_object(subject).get_set("GameEntity.abilities")) {
		const auto ability = view->get_object(std::dynamic_pointer_cast<nyan::ObjectValue>(entry.get_ptr())->get_name());
		if (not ability.extends("engine.ability.type.UseContingent")) {
			continue;
		}
		for (const auto &value : ability.get_set("UseContingent.amount")) {
			const auto amount = view->get_object(std::dynamic_pointer_cast<nyan::ObjectValue>(value.get_ptr())->get_name());
			if (amount.get_object("ResourceAmount.type").get_name() == "de2_base.data.util.resource.types.PopulationSpace") {
				population += get_nyan_int_or_float(amount, "ResourceAmount.amount");
			}
		}
	}
	return population;
}

const std::string resolve_file_path(const nyan::Object &obj, const std::string &path) {
	if (path.empty()) {
		return path;
	}

	if (path[0] == '/') {
		// absolute path
		std::regex modpack_id_regex("{[A-Za-z0-9_]+@[A-Za-z0-9_]+}");
		std::smatch match;
		if (std::regex_match(path, match, modpack_id_regex)) {
			auto modpack_id = match[0].str().substr(1, match[0].str().size() - 2);

			// TODO: get mount point of modpack

			auto path_end = match.suffix().str();

			return modpack_id + path_end;
		}
		else {
			throw Error(MSG(err) << "Absolute file path '" << path << "' does not contain a modpack ID.");
		}
	}
	else {
		// relative path
		std::string obj_path = obj.get_info().get_namespace().to_dirpath();
		return obj_path + "/" + path;
	}
}

} // namespace openage::gamestate::api
