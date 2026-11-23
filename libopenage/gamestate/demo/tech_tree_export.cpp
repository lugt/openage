// Copyright 2026 the openage authors. See copying.md for legal info.

#include "tech_tree_export.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <nyan/nyan.h>
#include <nyan/object_state.h>

#include "cvar/cvar.h"
#include "gamestate/game.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "time/time_loop.h"
#include "util/path.h"

namespace openage::gamestate::tests {

namespace {

QString q(const std::string &value) {
	return QString::fromStdString(value);
}

QJsonValue encode(const nyan::Value &value) {
	if (const auto ref = dynamic_cast<const nyan::ObjectValue *>(&value)) {
		return q(ref->get_name());
	}
	if (const auto text = dynamic_cast<const nyan::Text *>(&value)) {
		return q(text->get());
	}
	if (const auto integer = dynamic_cast<const nyan::Int *>(&value)) {
		return integer->is_infinite() ? QJsonValue{q(integer->str())} : QJsonValue{static_cast<qint64>(integer->get())};
	}
	if (const auto number = dynamic_cast<const nyan::Float *>(&value)) {
		return number->is_infinite() ? QJsonValue{q(number->str())} : QJsonValue{number->get()};
	}
	if (const auto set = dynamic_cast<const nyan::Set *>(&value)) {
		std::map<std::string, QJsonValue> sorted;
		for (const auto &entry : set->get()) {
			sorted.emplace(entry.get_ptr()->str(), encode(*entry.get_ptr()));
		}
		QJsonArray result;
		for (const auto &[key, entry] : sorted) {
			result.append(entry);
		}
		return result;
	}
	if (const auto ordered = dynamic_cast<const nyan::OrderedSet *>(&value)) {
		QJsonArray result;
		for (const auto &entry : ordered->get()) {
			result.append(encode(*entry.get_ptr()));
		}
		return result;
	}
	if (const auto dictionary = dynamic_cast<const nyan::Dict *>(&value)) {
		QJsonObject result;
		for (const auto &[key, entry] : dictionary->get()) {
			result.insert(key.get_ptr()->str().c_str(), encode(*entry.get_ptr()));
		}
		return result;
	}
	const auto text = value.str();
	if (text == "True") {
		return true;
	}
	if (text == "False") {
		return false;
	}
	if (text == "None") {
		return QJsonValue::Null;
	}
	return q(text);
}

std::set<std::string> members(const nyan::Object &object) {
	std::set<std::string> result;
	for (const auto &ancestor : object.get_linearized()) {
		const auto &raw = object.get_view()->get_raw(ancestor);
		for (const auto &[name, member] : raw->get_members()) {
			result.insert(name);
		}
	}
	return result;
}

QJsonObject fields(const nyan::Object &object) {
	QJsonObject result;
	if (object.is_patch()) {
		for (const auto &[name, member] : object.get_view()->get_raw(object.get_name())->get_members()) {
			result.insert(q(name), QJsonObject{
				{"operator", nyan::op_to_string(member.get_operation())},
				{"value", encode(member.get_value())},
			});
		}
	}
	else {
		for (const auto &member : members(object)) {
			result.insert(q(member), encode(*object.get_value(member).get_ptr()));
		}
	}
	return result;
}

QJsonObject record(const nyan::Object &object) {
	QJsonArray parents;
	for (const auto &parent : object.get_parents()) {
		parents.append(q(parent));
	}
	QJsonObject result{
		{"parents", parents},
		{"fields", fields(object)},
	};
	if (const auto target = object.get_target()) {
		result.insert("target", q(*target));
	}
	return result;
}

std::set<std::string> children(const std::shared_ptr<nyan::View> &view, const std::string &type) {
	auto values = view->get_obj_children_all(type);
	std::set<std::string> result;
	for (const auto &id : values) {
		if (id.starts_with("de2_base.")) {
			result.insert(id);
		}
	}
	return result;
}

void write_json(const QString &path, const QJsonObject &value) {
	QSaveFile file{path};
	if (not file.open(QIODevice::WriteOnly)) {
		throw std::runtime_error{"Cannot open tech tree output: " + path.toStdString()};
	}
	const auto bytes = QJsonDocument{value}.toJson(QJsonDocument::Compact);
	if (file.write(bytes) != bytes.size() or not file.commit()) {
		throw std::runtime_error{"Cannot write tech tree output: " + path.toStdString()};
	}
}

} // namespace

void simulation_demo_52(const util::Path &path) {
	const auto output = std::getenv("OPENAGE_TECH_TREE_OUTPUT");
	if (not output or not *output) {
		throw std::runtime_error{"Set OPENAGE_TECH_TREE_OUTPUT to the output JSON filename"};
	}

	auto cvar = std::make_shared<cvar::CVarManager>(path);
	auto clock = std::make_shared<time::TimeLoop>();
	auto simulation = std::make_shared<GameSimulation>(path, cvar, clock);
	simulation->set_modpacks({"de2_base"});
	simulation->initialize();
	const auto view = simulation->get_game()->get_state()->get_db_view();

	const std::vector<std::string> types{
		"engine.util.setup.PlayerSetup",
		"engine.util.game_entity.GameEntity",
		"engine.util.tech.Tech",
		"engine.util.create.CreatableGameEntity",
		"engine.util.research.ResearchableTech",
		"engine.util.logic.LogicElement",
		"engine.util.progress_status.ProgressStatus",
		"engine.util.cost.Cost",
		"engine.util.resource.ResourceAmount",
		"engine.util.patch.Patch",
		"engine.util.patch.NyanPatch",
		"engine.ability.Ability",
		"engine.util.production_mode.ProductionMode",
	};
	std::set<std::string> ids;
	QJsonObject categories;
	for (const auto &type : types) {
		QJsonArray list;
		for (const auto &id : children(view, type)) {
			ids.insert(id);
			list.append(q(id));
		}
		categories.insert(q(type), list);
	}

	// Include effect targets so each exported patch has its base values.
	for (const auto &id : categories.value("engine.util.patch.NyanPatch").toArray()) {
		const auto patch = view->get_object(id.toString().toStdString());
		if (const auto target = patch.get_target()) {
			ids.insert(*target);
		}
	}
	QJsonObject objects;
	for (const auto &id : ids) {
		objects.insert(q(id), record(view->get_object(id)));
	}

	QJsonArray civilizations;
	int failures = 0;
	for (const auto &id : children(view, "engine.util.setup.PlayerSetup")) {
		if (not id.starts_with("de2_base.data.civ.")) {
			continue;
		}
		const auto civ_view = view->new_child();
		const auto setup = civ_view->get_object(id);
		QJsonObject civ{
			{"id", q(id)},
			{"gaia", id == "de2_base.data.civ.gaia.gaia.Gaia"},
		};
		QJsonArray patches;
		QJsonObject changed;
		std::set<std::string> targets;
		try {
			auto transaction = civ_view->new_transaction(0);
			for (const auto &entry : setup.get_orderedset("PlayerSetup.game_setup")) {
				const auto ref = std::dynamic_pointer_cast<nyan::ObjectValue>(entry.get_ptr());
				auto patch = civ_view->get_object(ref->get_name());
				patches.append(q(patch.get_name()));
				if (patch.extends("engine.util.patch.Patch")) {
					patch = patch.get_object("Patch.patch");
				}
				const auto target = patch.get_target();
				if (not target) {
					throw std::runtime_error{"Civilization patch has no target: " + patch.get_name()};
				}
				targets.insert(*target);
				transaction.add(patch);
			}
			if (not transaction.commit()) {
				throw std::runtime_error{"Civilization patch transaction failed"};
			}
			// Parent patches may change abilities inherited by other objects.
			// Export all changed catalog objects, not only direct targets.
			for (const auto &object_id : ids) {
				const auto object = civ_view->get_object(object_id);
				if (object.is_patch()) {
					continue;
				}
				bool affected = targets.contains(object_id);
				for (const auto &ancestor : object.get_linearized()) {
					affected = affected or targets.contains(ancestor);
				}
				if (affected) {
					const auto data = record(object);
					if (data != objects.value(q(object_id)).toObject()) {
						changed.insert(q(object_id), data);
					}
				}
			}
			civ.insert("status", "ok");
		}
		catch (const std::exception &error) {
			civ.insert("status", "failed");
			civ.insert("error", error.what());
			++failures;
		}
		civ.insert("setup", record(setup));
		civ.insert("patches", patches);
		civ.insert("overrides", changed);
		civilizations.append(civ);
		log::log(MSG(info) << "TECH TREE CIV " << id << " " << civ.value("status").toString().toStdString());
	}

	write_json(QString::fromUtf8(output), QJsonObject{
		{"schema", "openage-tech-tree-native-v1"},
		{"categories", categories},
		{"objects", objects},
		{"civilizations", civilizations},
		{"failures", failures},
	});
	if (failures) {
		throw std::runtime_error{"Some civilizations failed export. Inspect the JSON report."};
	}
	log::log(MSG(info) << "DEMO 52: PASS (exported " << civilizations.size()
	                   << " civilizations and " << objects.size() << " nyan objects)");
}

} // namespace openage::gamestate::tests
