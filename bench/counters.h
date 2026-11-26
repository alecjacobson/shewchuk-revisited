// CPU counters for the benchmark harness.
//
// Wall-clock time is not a usable metric on a laptop: the clock frequency
// moves with temperature and load, so the same code is measured at very
// different nanoseconds per call while its cycle count stays put. The harness
// therefore measures CPU cycles and retired instructions where the platform
// lets an unprivileged process read them:
//
//   macOS  proc_pid_rusage(RUSAGE_INFO_V6): cycles, instructions and the
//          share of cycles spent on performance cores, for this process.
//   Linux  perf_event_open: user-mode cycles and instructions of this thread.
//          Refused on most virtual machines and with a restrictive
//          kernel.perf_event_paranoid.
//
// Everywhere else, and whenever the counters are refused or read zero, the
// backend is "wall-clock": only nanoseconds are measured and every output is
// labelled accordingly. Nothing is ever estimated from the clock and reported
// as cycles.
#pragma once

#include <cstdint>
#include <string>

namespace shewchuk_bench {

struct CounterSample {
  std::uint64_t ns = 0;            // monotonic clock
  std::uint64_t cycles = 0;        // 0 unless has_cycles
  std::uint64_t instructions = 0;  // 0 unless has_cycles
  std::uint64_t pcore_cycles = 0;  // 0 unless has_core_type
};

struct CounterInfo {
  std::string backend;          // "macos-proc_pid_rusage", "linux-perf_event_open", "wall-clock"
  std::string fallback_reason;  // why the backend is wall-clock, else empty
  bool has_cycles = false;      // cycles and instructions are measured
  bool has_core_type = false;   // pcore_cycles is measured
};

// Selects the backend. Call once before counters_read().
// force_wall_clock skips the hardware counters (used to test the fallback).
const CounterInfo& counters_init(bool force_wall_clock);

// Reads all counters. Costs a system call when hardware counters are used.
CounterSample counters_read();

// Computes for up to about half a second until this thread runs on a
// performance core (judged by the same share of cycles the chunk filter
// uses). Returns false if it never did. Always true without core types.
bool wait_for_performance_core(double min_share);

// The number of the CPU this thread is running on, or -1 if unknown.
int current_cpu();

// Asks the scheduler for performance cores where there is such a request
// (macOS quality-of-service class). Returns a description of what was done.
std::string request_performance_cores();

}  // namespace shewchuk_bench
