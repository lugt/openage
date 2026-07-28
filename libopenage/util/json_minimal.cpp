// Copyright 2026 the openage authors. See copying.md for legal info.

#include "json_minimal.h"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <utility>


namespace openage::util::json {


// ---------------------------------------------------------------------------
// Value navigation
// ---------------------------------------------------------------------------


const Value &Value::at(const std::string &key) const {
	if (this->type != Type::OBJECT) {
		throw JsonError{"at(key): value is not an object (type="
		                + std::to_string(static_cast<int>(this->type)) + ")"};
	}
	auto it = this->obj_val.find(key);
	if (it == this->obj_val.end()) {
		throw JsonError{"at(key): object has no key \"" + key + "\""};
	}
	return it->second;
}


bool Value::has(const std::string &key) const {
	if (this->type != Type::OBJECT) {
		return false;
	}
	return this->obj_val.find(key) != this->obj_val.end();
}


// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------


class Parser {
public:
	explicit Parser(const std::string &text) : text_{text} {}

	Value parse() {
		this->skip_ws();
		Value v = this->parse_value();
		this->skip_ws();
		if (this->pos_ != this->text_.size()) {
			throw this->err("trailing content after JSON value");
		}
		return v;
	}

private:
	const std::string &text_;
	size_t pos_{0};

	JsonError err(const std::string &msg) const {
		return JsonError{"json: " + msg + " at offset "
		                  + std::to_string(this->pos_)};
	}

	void skip_ws() {
		while (this->pos_ < this->text_.size()) {
			char c = this->text_[this->pos_];
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
				this->pos_++;
			}
			else {
				break;
			}
		}
	}

	// ---- value dispatch ---------------------------------------------

	Value parse_value() {
		this->skip_ws();
		if (this->pos_ >= this->text_.size()) {
			throw this->err("unexpected end of input");
		}
		char c = this->text_[this->pos_];
		if (c == '{') return this->parse_object();
		if (c == '[') return this->parse_array();
		if (c == '"') return Value{this->parse_string()};
		if (c == 't' || c == 'f') return this->parse_bool();
		if (c == 'n') return this->parse_null();
		if (c == '-' || (c >= '0' && c <= '9')) {
			return this->parse_number();
		}
		throw this->err(std::string{"unexpected character '"} + c + "'");
	}

	// ---- object ------------------------------------------------------

	Value parse_object() {
		if (this->text_[this->pos_] != '{') {
			throw this->err("expected '{'");
		}
		this->pos_++;
		Object obj;
		this->skip_ws();
		if (this->pos_ < this->text_.size() && this->text_[this->pos_] == '}') {
			this->pos_++;
			return Value{std::move(obj)};
		}
		while (true) {
			this->skip_ws();
			std::string key = this->parse_string();
			this->skip_ws();
			if (this->pos_ >= this->text_.size() || this->text_[this->pos_] != ':') {
				throw this->err("expected ':' after object key");
			}
			this->pos_++;
			Value v = this->parse_value();
			obj.emplace(std::move(key), std::move(v));
			this->skip_ws();
			if (this->pos_ >= this->text_.size()) {
				throw this->err("unexpected end of input in object");
			}
			char c = this->text_[this->pos_];
			if (c == ',') {
				this->pos_++;
				continue;
			}
			if (c == '}') {
				this->pos_++;
				return Value{std::move(obj)};
			}
			throw this->err(std::string{"expected ',' or '}' in object, got '"} + c + "'");
		}
	}

	// ---- array -------------------------------------------------------

	Value parse_array() {
		if (this->text_[this->pos_] != '[') {
			throw this->err("expected '['");
		}
		this->pos_++;
		Array arr;
		this->skip_ws();
		if (this->pos_ < this->text_.size() && this->text_[this->pos_] == ']') {
			this->pos_++;
			return Value{std::move(arr)};
		}
		while (true) {
			arr.push_back(this->parse_value());
			this->skip_ws();
			if (this->pos_ >= this->text_.size()) {
				throw this->err("unexpected end of input in array");
			}
			char c = this->text_[this->pos_];
			if (c == ',') {
				this->pos_++;
				continue;
			}
			if (c == ']') {
				this->pos_++;
				return Value{std::move(arr)};
			}
			throw this->err(std::string{"expected ',' or ']' in array, got '"} + c + "'");
		}
	}

	// ---- string ------------------------------------------------------

	std::string parse_string() {
		if (this->pos_ >= this->text_.size() || this->text_[this->pos_] != '"') {
			throw this->err("expected '\"'");
		}
		this->pos_++;
		std::string out;
		while (true) {
			if (this->pos_ >= this->text_.size()) {
				throw this->err("unterminated string");
			}
			char c = this->text_[this->pos_];
			if (c == '"') {
				this->pos_++;
				return out;
			}
			if (c == '\\') {
				if (this->pos_ + 1 >= this->text_.size()) {
					throw this->err("dangling escape");
				}
				char esc = this->text_[this->pos_ + 1];
				switch (esc) {
				case '"': out.push_back('"'); break;
				case '\\': out.push_back('\\'); break;
				case '/': out.push_back('/'); break;
				case 'b': out.push_back('\b'); break;
				case 'f': out.push_back('\f'); break;
				case 'n': out.push_back('\n'); break;
				case 'r': out.push_back('\r'); break;
				case 't': out.push_back('\t'); break;
				case 'u': {
					// Minimal \uXXXX support: decode to UTF-8 (only the
					// BMP basic range, no surrogate pairs).
					if (this->pos_ + 6 > this->text_.size()) {
						throw this->err("truncated \\u escape");
					}
					uint32_t code = 0;
					for (int i = 0; i < 4; ++i) {
						char h = this->text_[this->pos_ + 2 + i];
						code <<= 4;
						if (h >= '0' && h <= '9') code |= (h - '0');
						else if (h >= 'a' && h <= 'f') code |= (h - 'a' + 10);
						else if (h >= 'A' && h <= 'F') code |= (h - 'A' + 10);
						else {
							throw this->err("invalid hex digit in \\u escape");
						}
					}
					if (code < 0x80) {
						out.push_back(static_cast<char>(code));
					}
					else if (code < 0x800) {
						out.push_back(static_cast<char>(0xC0 | (code >> 6)));
						out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
					}
					else {
						out.push_back(static_cast<char>(0xE0 | (code >> 12)));
						out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
						out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
					}
					this->pos_ += 4;  // 4 hex digits, plus the 'u' below
					break;
				}
				default:
					throw this->err(std::string{"invalid escape \\"} + esc);
				}
				this->pos_ += 2;
				continue;
			}
			out.push_back(c);
			this->pos_++;
		}
	}

	// ---- bool / null --------------------------------------------------

	Value parse_bool() {
		if (this->text_.compare(this->pos_, 4, "true") == 0) {
			this->pos_ += 4;
			return Value{true};
		}
		if (this->text_.compare(this->pos_, 5, "false") == 0) {
			this->pos_ += 5;
			return Value{false};
		}
		throw this->err("invalid boolean literal");
	}

	Value parse_null() {
		if (this->text_.compare(this->pos_, 4, "null") == 0) {
			this->pos_ += 4;
			return Value{};
		}
		throw this->err("invalid null literal");
	}

	// ---- number -------------------------------------------------------

	Value parse_number() {
		size_t start = this->pos_;
		if (this->text_[this->pos_] == '-') this->pos_++;
		bool is_float = false;
		while (this->pos_ < this->text_.size()) {
			char c = this->text_[this->pos_];
			if (c >= '0' && c <= '9') {
				this->pos_++;
			}
			else if (c == '.') {
				is_float = true;
				this->pos_++;
			}
			else if (c == 'e' || c == 'E') {
				is_float = true;
				this->pos_++;
				if (this->pos_ < this->text_.size()
					&& (this->text_[this->pos_] == '+' || this->text_[this->pos_] == '-')) {
					this->pos_++;
				}
			}
			else {
				break;
			}
		}
		std::string sub = this->text_.substr(start, this->pos_ - start);
		if (sub.empty()) {
			throw this->err("invalid number literal");
		}
		try {
			if (is_float) {
				return Value{std::stod(sub)};
			}
			return Value{static_cast<int64_t>(std::stoll(sub))};
		}
		catch (const std::exception &e) {
			throw this->err(std::string{"number parse failed: "} + e.what());
		}
	}
};


Value parse(const std::string &text) {
	Parser p{text};
	return p.parse();
}


Value parse_file(const std::string &path) {
	std::ifstream in{path};
	if (!in) {
		throw JsonError{"parse_file: cannot open " + path};
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	return parse(ss.str());
}


}  // namespace openage::util::json