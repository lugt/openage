// Copyright 2026+ the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "gamestate/types.h"
#include "pyinterface/pyobject.h"


namespace openage::gamestate {
class GameSimulation;

/**
 * Kind of entry stored in the in-game chat and command console history.
 */
enum class ChatEntryKind : uint8_t {
	CHAT,
	SYSTEM,
	CHEAT,
	PYTHON,
	ERROR,
};

/**
 * Single line in the chat / debug console history.
 */
struct ChatEntry {
	player_id_t sender{0};
	std::string sender_name;
	std::string text;
	ChatEntryKind kind{ChatEntryKind::CHAT};
};

/**
 * Result of executing one line submitted via the in-game chat box.
 */
struct ChatCommandResult {
	bool is_command{false};
	bool handled{false};
	bool is_error{false};
	std::string response;
};

/**
 * Processes in-game chat messages (reserved for multiplayer), AoE2-compatible
 * cheat codes, openage debug commands (/help, /spawn, /speed, ...), and
 * inline Python expressions/statements (py ..., /py ..., >>> ...).
 */
class ChatConsole {
public:
	explicit ChatConsole(GameSimulation &sim, player_id_t local_player = 0);
	~ChatConsole() = default;

	/**
	 * Execute a raw line entered in the chat dialog.
	 */
	ChatCommandResult execute(const std::string &raw_input, player_id_t sender_player = 0);

	/**
	 * Enable or disable AoE2 cheat codes (e.g. multiplayer match rules).
	 */
	void set_cheats_enabled(bool enabled) { this->cheats_enabled = enabled; }
	bool are_cheats_enabled() const { return this->cheats_enabled; }

	bool is_muted() const { return this->muted; }

	/**
	 * Snapshot of recent chat/console entries (thread-safe).
	 */
	std::vector<ChatEntry> get_history() const;

	void clear_history();

private:
	ChatCommandResult try_execute_debug_command(const std::string &trimmed, player_id_t sender);
	ChatCommandResult try_execute_python(const std::string &code);
	ChatCommandResult try_execute_cheat(const std::string &trimmed, player_id_t sender);

	void push_entry(player_id_t sender, std::string sender_name, std::string text, ChatEntryKind kind);
	void ensure_python_env();

	GameSimulation &sim;
	player_id_t local_player{0};
	bool cheats_enabled{true};
	bool muted{false};
	bool natural_wonders_active{false};

	mutable std::mutex mutex;
	std::vector<ChatEntry> history;
	pyinterface::PyObjectRef py_globals;
	bool py_initialized{false};
};

} // namespace openage::gamestate
