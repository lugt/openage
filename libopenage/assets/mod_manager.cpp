// Copyright 2023-2023 the openage authors. See copying.md for legal info.

#include "mod_manager.h"

#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace openage::assets {

ModManager::ModManager(const util::Path &asset_base_dir) :
	asset_base_dir{asset_base_dir} {
}

void ModManager::register_modpack(const util::Path &info_file) {
	auto info = parse_modepack_def(info_file);
	this->available.emplace(info.id, info);
}

void ModManager::register_modpack(const ModpackInfo &info) {
	this->available.emplace(info.id, info);
}

void ModManager::activate_modpacks(const std::vector<std::string> &load_order) {
	this->set_load_order(load_order);

	// Clear in case there are already active modpacks
	this->active.clear();
	this->dump_state("activate_modpacks:after-clear");

	for (const auto &modpack_id : load_order) {
		auto &modpack = this->available.at(modpack_id);
		this->active.emplace(modpack_id, std::make_shared<Modpack>(modpack));
		log::log(MSG(info) << "Activated modpack: " << modpack_id);
	}

	// Phase C5 invariant: a successful return from activate_modpacks must
	// have every load_order entry present in active. Otherwise downstream
	// get_modpack() calls will throw "Modpack not loaded" with no observed
	// root cause -- this assert cuts the diagnostic tail at the source.
	for (const auto &modpack_id : load_order) {
		if (not this->active.contains(modpack_id)) {
			this->dump_state("activate_modpacks:INVARIANT-FAIL");
			throw Error{ERR
			             << "ModManager invariant failed: activate_modpacks emplace reported success for '"
			             << modpack_id
			             << "' (line above) but active does not contain it. "
			             << "This is either a non-atomic emplace/clear, a moved-from or dangling ModManager, "
			             << "or a concurrent writer. Inspect the dump_state dumps bracketing this call."};
		}
	}
	this->dump_state("activate_modpacks:after-fill-ok");
}

void ModManager::set_load_order(const std::vector<std::string> &load_order) {
	std::unordered_set<std::string> requested{load_order.begin(), load_order.end()};

	// sanity check: availability, dependencies, conflicts
	for (const auto &modpack_id : load_order) {
		if (not this->available.contains(modpack_id)) {
			throw Error{MSG(err) << "Requested modpack '" << modpack_id << "' not available."};
		}

		auto &info = this->available.at(modpack_id);
		for (const auto &dependency : info.dependencies) {
			if (not requested.contains(dependency)) {
				throw Error{MSG(err) << "Dependency '" << dependency << "' of modpack '"
				                     << modpack_id << "' not satisfied"};
			}
		}

		for (const auto &conflict : info.conflicts) {
			if (requested.contains(conflict)) {
				throw Error{MSG(err) << "Modpack '" << modpack_id << "' conflicts with modpack '"
				                     << conflict << "'"};
			}
		}
	}

	this->load_order = load_order;
}

std::shared_ptr<Modpack> ModManager::get_modpack(const std::string &modpack_id) const {
	if (not this->active.contains(modpack_id)) {
		// Phase C5 minimal diagnostic (NO iteration of active/available):
		// the previous full dump_state issued from this throw path
		// SIGSEGV'd inside std::basic_string::_M_construct, so iterate
		// paths are suspect. Just emit pointer + size + key. If this
		// succeeds the corruption sits in map internals, otherwise it
		// is in the key string itself (i.e. caller supplied a corrupt
		// `modpack_id`).
		log::log(INFO << "[ModManager@" << static_cast<const void *>(this)
		              << " get_modpack:throw modpack=\"" << modpack_id
		              << "\" active_size=" << this->active.size() << "]");
		throw Error{MSG(err) << "Modpack not loaded: " << modpack_id};
	}

	return this->active.at(modpack_id);
}

void ModManager::dump_state(const std::string &label) const {
	// Phase G1.5: the default closure log is concise -- sorted
	// available/active/load_order and a per-id matrix. Raw pointer
	// addresses and the original six-boundary "labels" are dropped to
	// avoid a noisy default log; if a reviewer needs to compare two
	// observation points at object-identity granularity, re-enable the
	// pointer prefix via OPENAGE_MOD_MGR_DUMP_POINTERS=1.
	const bool dump_addresses = std::getenv("OPENAGE_MOD_MGR_DUMP_POINTERS") != nullptr;

	std::vector<std::string> avail_keys;
	avail_keys.reserve(this->available.size());
	for (const auto &kv : this->available) {
		avail_keys.push_back(kv.first);
	}
	std::sort(avail_keys.begin(), avail_keys.end());

	std::vector<std::string> active_keys;
	active_keys.reserve(this->active.size());
	for (const auto &kv : this->active) {
		active_keys.push_back(kv.first);
	}
	std::sort(active_keys.begin(), active_keys.end());

	std::ostringstream oss;
	if (dump_addresses) {
		oss << "[ModManager@" << static_cast<const void *>(this) << " ";
	}
	oss << "label=" << label
	    << " avail=" << avail_keys.size()
	    << " active=" << active_keys.size()
	    << " load_order=" << this->load_order.size()
	    << " | avail=[";
	for (size_t i = 0; i < avail_keys.size(); ++i) {
		if (i) oss << ",";
		oss << avail_keys[i];
	}
	oss << "] | active=[";
	for (size_t i = 0; i < active_keys.size(); ++i) {
		if (i) oss << ",";
		oss << active_keys[i];
	}
	oss << "] | load_order=[";
	for (size_t i = 0; i < this->load_order.size(); ++i) {
		if (i) oss << ",";
		oss << this->load_order[i];
	}
	oss << "]";
	oss << " | matrix=[";
	for (size_t i = 0; i < this->load_order.size(); ++i) {
		const auto &id = this->load_order[i];
		if (i) oss << ",";
		oss << id
		    << ":avail=" << (this->available.contains(id) ? "Y" : "N")
		    << "/active=" << (this->active.contains(id) ? "Y" : "N");
	}
	oss << "]";

	log::log(INFO << oss.str());
}

const std::vector<std::string> &ModManager::get_load_order() const {
	return this->load_order;
}

} // namespace openage::assets
