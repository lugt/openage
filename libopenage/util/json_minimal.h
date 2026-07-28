// Copyright 2026 the openage authors. See copying.md for legal info.
//
// Minimal JSON parser used by the Mission Pack reader (P2.7).
//
// Scope:
//   - parses objects, arrays, strings, numbers (int + float), booleans,
//     and null; that is sufficient for the deterministic output of
//     ``openage.scenario.writer.write_mission_pack``.
//   - the parser is *intentionally minimal*: no schema, no DOM API,
//     no streaming. The Mission Pack reader wraps the output tree in
//     a typed ``MissionPack`` struct.
//
// Out of scope:
//   - Unicode escapes beyond \\uXXXX -> UTF-8 (the writer never emits
//     them; the reader tolerates them but does not normalise).
//   - NaN / Infinity literal support.
//   - Strict RFC 8259 conformance.

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>


namespace openage::util::json {


class JsonError : public std::runtime_error {
public:
	explicit JsonError(const std::string &msg) : std::runtime_error{msg} {}
};


// Forward declaration of the recursive value type.
class Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;


// A JSON value is one of: object, array, string, number (int|float),
// boolean, or null.
class Value {
public:
	enum class Type {
		OBJECT, ARRAY, STRING, INT, FLOAT, BOOL, NULL_,
	};

	Value() : type{Type::NULL_}, int_val{0} {}  // NOLINT
	explicit Value(Object obj) : type{Type::OBJECT}, obj_val{std::move(obj)} {}
	explicit Value(Array arr) : type{Type::ARRAY}, arr_val{std::move(arr)} {}
	explicit Value(std::string s) : type{Type::STRING}, str_val{std::move(s)} {}
	explicit Value(int64_t i) : type{Type::INT}, int_val{i} {}
	explicit Value(double f) : type{Type::FLOAT}, float_val{f} {}
	explicit Value(bool b) : type{Type::BOOL}, bool_val{b} {}

	Type get_type() const { return this->type; }

	bool is_object() const { return this->type == Type::OBJECT; }
	bool is_array() const { return this->type == Type::ARRAY; }
	bool is_string() const { return this->type == Type::STRING; }
	bool is_int() const { return this->type == Type::INT; }
	bool is_float() const { return this->type == Type::FLOAT; }
	bool is_number() const { return this->type == Type::INT || this->type == Type::FLOAT; }
	bool is_bool() const { return this->type == Type::BOOL; }
	bool is_null() const { return this->type == Type::NULL_; }

	const Object &as_object() const { return this->obj_val; }
	const Array &as_array() const { return this->arr_val; }
	const std::string &as_string() const { return this->str_val; }
	int64_t as_int() const { return this->int_val; }
	double as_float() const {
		return this->type == Type::INT
			? static_cast<double>(this->int_val)
			: this->float_val;
	}
	double as_number() const { return this->as_float(); }
	bool as_bool() const { return this->bool_val; }

	// Object navigation helpers. Missing keys raise JsonError (rather
	// than silently returning a null default) so the reader surfaces
	// schema drift instead of falling back to bogus defaults.
	const Value &at(const std::string &key) const;
	bool has(const std::string &key) const;

private:
	Type type;
	Object obj_val;
	Array arr_val;
	std::string str_val;
	int64_t int_val;
	double float_val;
	bool bool_val;
};


// Parse a JSON document from a string. Raises JsonError on malformed
// input. The returned value owns its children.
Value parse(const std::string &text);


// Parse a JSON document from a file path. Raises JsonError on malformed
// input or I/O failure.
Value parse_file(const std::string &path);


}  // namespace openage::util::json