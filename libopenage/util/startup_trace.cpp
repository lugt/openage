// Copyright 2026+ the openage authors. See copying.md for legal info.

module;

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <fstream>
#include <sys/resource.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

#include "log/log.h"
#include "log/message.h"
#include "util/timing.h"

export module openage.util.startup_trace;

export namespace openage::util {

/**
 * Snapshot of the current process's resident memory usage.
 */
struct MemorySnapshot {
	uint64_t rss_bytes = 0;
	uint64_t peak_rss_bytes = 0;
};

/**
 * Query current and peak resident set size (RSS) of the running process.
 */
MemorySnapshot query_process_memory();

/**
 * Format a byte count or signed byte delta in MiB with two decimal places.
 */
std::string format_mib(double bytes, bool show_sign = false);

/**
 * Log a single startup milestone relative to an origin monotonic timestamp.
 */
void log_startup_milestone(std::string_view stage, time_nsec_t origin_ns);

/**
 * RAII timer and memory delta tracer for startup stages.
 *
 * Logs elapsed wall-clock time and process RSS delta when the scope ends
 * (or earlier if finish() is called explicitly).
 */
class StartupTraceScope final {
public:
	explicit StartupTraceScope(std::string stage);
	~StartupTraceScope();

	StartupTraceScope(const StartupTraceScope &) = delete;
	StartupTraceScope &operator=(const StartupTraceScope &) = delete;
	StartupTraceScope(StartupTraceScope &&) = delete;
	StartupTraceScope &operator=(StartupTraceScope &&) = delete;

	/**
	 * Emit the trace log immediately and mark this scope as finished.
	 */
	void finish();

private:
	std::string stage;
	time_nsec_t start_ns;
	MemorySnapshot start_mem;
	bool finished = false;
};

} // namespace openage::util

namespace openage::util {

MemorySnapshot query_process_memory() {
	MemorySnapshot snap{};

#if defined(__APPLE__)
	mach_task_basic_info_data_t info{};
	mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
	if (task_info(mach_task_self(),
	              MACH_TASK_BASIC_INFO,
	              reinterpret_cast<task_info_t>(&info),
	              &count)
	    == KERN_SUCCESS) {
		snap.rss_bytes = static_cast<uint64_t>(info.resident_size);
		snap.peak_rss_bytes = static_cast<uint64_t>(info.resident_size_max);
	}
#elif defined(__linux__)
	std::ifstream statm{"/proc/self/statm"};
	uint64_t total_pages = 0;
	uint64_t resident_pages = 0;
	if (statm >> total_pages >> resident_pages) {
		const long page_size = sysconf(_SC_PAGESIZE);
		if (page_size > 0) {
			snap.rss_bytes = resident_pages * static_cast<uint64_t>(page_size);
		}
	}
	rusage usage{};
	if (getrusage(RUSAGE_SELF, &usage) == 0 && usage.ru_maxrss > 0) {
		snap.peak_rss_bytes = static_cast<uint64_t>(usage.ru_maxrss) * 1024ULL;
	}
#elif defined(_WIN32)
	PROCESS_MEMORY_COUNTERS pmc{};
	if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
		snap.rss_bytes = static_cast<uint64_t>(pmc.WorkingSetSize);
		snap.peak_rss_bytes = static_cast<uint64_t>(pmc.PeakWorkingSetSize);
	}
#endif

	return snap;
}

std::string format_mib(double bytes, bool show_sign) {
	constexpr double MIB = 1024.0 * 1024.0;
	char buf[32];
	const double mib = bytes / MIB;
	if (show_sign) {
		std::snprintf(buf, sizeof(buf), "%+.2f", mib);
	}
	else {
		std::snprintf(buf, sizeof(buf), "%.2f", mib);
	}
	return std::string{buf};
}

namespace {

std::string format_ms(time_nsec_t elapsed_ns) {
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(elapsed_ns) / 1e6);
	return std::string{buf};
}

} // namespace

void log_startup_milestone(std::string_view stage, time_nsec_t origin_ns) {
	const time_nsec_t now_ns = timing::get_monotonic_time();
	const time_nsec_t elapsed_ns = now_ns >= origin_ns ? (now_ns - origin_ns) : 0;
	const MemorySnapshot mem = query_process_memory();

	log::log(INFO << "[trace] " << stage << ": "
	              << format_ms(elapsed_ns) << " ms since start"
	              << " | rss=" << format_mib(static_cast<double>(mem.rss_bytes)) << " MiB"
	              << " (peak=" << format_mib(static_cast<double>(mem.peak_rss_bytes)) << " MiB)");
}

StartupTraceScope::StartupTraceScope(std::string stage) :
	stage{std::move(stage)},
	start_ns{timing::get_monotonic_time()},
	start_mem{query_process_memory()} {}

StartupTraceScope::~StartupTraceScope() {
	this->finish();
}

void StartupTraceScope::finish() {
	if (this->finished) {
		return;
	}
	this->finished = true;

	const time_nsec_t end_ns = timing::get_monotonic_time();
	const time_nsec_t elapsed_ns = end_ns >= this->start_ns ? (end_ns - this->start_ns) : 0;
	const MemorySnapshot end_mem = query_process_memory();
	const double delta_bytes =
		static_cast<double>(end_mem.rss_bytes) - static_cast<double>(this->start_mem.rss_bytes);

	log::log(INFO << "[trace] " << this->stage << ": "
	              << format_ms(elapsed_ns) << " ms"
	              << " | rss=" << format_mib(static_cast<double>(end_mem.rss_bytes)) << " MiB"
	              << " (delta=" << format_mib(delta_bytes, true) << " MiB"
	              << ", peak=" << format_mib(static_cast<double>(end_mem.peak_rss_bytes)) << " MiB)");
}

} // namespace openage::util
