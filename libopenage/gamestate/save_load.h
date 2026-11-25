// Copyright 2026+ the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>
#include <string>
#include <vector>


namespace openage::util {
class Path;
}

namespace openage::gamestate {
class GameSimulation;

/**
 * Metadata for a saved game file on disk.
 */
struct SaveSlotInfo {
	std::string name;
	std::string path;
	std::string summary;
};

/**
 * Serialize/deserialize live GameSimulation state for Save & Load.
 */
class GameSaveManager {
public:
	/** Resolve the writable save location without requiring it to exist. */
	static std::string directory(const util::Path &root);

	static std::string serialize(GameSimulation &sim);

	static bool deserialize(GameSimulation &sim,
	                        const std::string &snapshot_text,
	                        std::string *error_out = nullptr);

	static bool save_to_file(GameSimulation &sim,
	                         const std::string &file_path,
	                         std::string *error_out = nullptr);

	static bool load_from_file(GameSimulation &sim,
	                           const std::string &file_path,
	                           std::string *error_out = nullptr);

	static std::vector<SaveSlotInfo> list_saves(const std::string &saves_dir);
};

} // namespace openage::gamestate
