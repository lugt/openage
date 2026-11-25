// Copyright 2026+ the openage authors. See copying.md for legal info.

module;

#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "error/error.h"
#include "log/message.h"

export module openage.util.snapshot_text;

export namespace openage::util {

/** Format exact hundredths as locale-independent ASCII with two decimal places. */
std::string format_centi(int64_t centi);

/** Parse exact hundredths; reject excess precision and malformed numbers. */
int64_t parse_centi(std::string_view text);

} // namespace openage::util

namespace openage::util {

std::string format_centi(int64_t centi) {
	bool neg = centi < 0;
	uint64_t mag = neg ? static_cast<uint64_t>(-(centi + 1)) + 1ULL : static_cast<uint64_t>(centi);
	uint64_t whole = mag / 100ULL;
	uint64_t frac = mag % 100ULL;

	std::string out;
	if (neg and mag > 0) {
		out.push_back('-');
	}
	out += std::to_string(whole);
	out.push_back('.');
	out.push_back(static_cast<char>('0' + (frac / 10ULL)));
	out.push_back(static_cast<char>('0' + (frac % 10ULL)));
	return out;
}


int64_t parse_centi(std::string_view text) {
	bool negative = not text.empty() and text.front() == '-';
	if (negative) {
		text.remove_prefix(1);
	}
	auto dot = text.find('.');
	auto whole_text = text.substr(0, dot);
	uint64_t whole{};
	auto result = std::from_chars(whole_text.data(), whole_text.data() + whole_text.size(), whole);
	if (whole_text.empty() or result.ec != std::errc{} or result.ptr != whole_text.data() + whole_text.size()) {
		throw Error{MSG(err) << "Invalid fixed-point number"};
	}
	uint64_t fraction{};
	if (dot != std::string_view::npos) {
		auto digits = text.substr(dot + 1);
		if (digits.empty() or digits.size() > 2) {
			throw Error{MSG(err) << "Expected one or two decimal places"};
		}
		for (char digit : digits) {
			if (digit < '0' or digit > '9') {
				throw Error{MSG(err) << "Invalid fractional digit"};
			}
			fraction = fraction * 10 + static_cast<unsigned>(digit - '0');
		}
		if (digits.size() == 1) {
			fraction *= 10;
		}
	}
	uint64_t limit = static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + negative;
	if (whole > (limit - fraction) / 100) {
		throw Error{MSG(err) << "Fixed-point number out of range"};
	}
	uint64_t magnitude = whole * 100 + fraction;
	if (negative and magnitude != 0) {
		return -static_cast<int64_t>(magnitude - 1) - 1;
	}
	return static_cast<int64_t>(magnitude);
}

} // namespace openage::util
