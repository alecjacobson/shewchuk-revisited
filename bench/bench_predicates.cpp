// Benchmark driver. See docs/benchmarking.md.
//
// One process measures one implementation: for every selected (predicate,
// workload) it makes a single pass over a stream of inputs it has never seen,
// split into separately measured chunks, and reports CPU cycles, instructions
// and nanoseconds per call. Run it again (a fresh process) for a repetition;
// tools/bench_suite.py and tools/compare_revs.py do that.
//
// Exit status: 0 measured; 1 checksum mismatch or workload error; 2 usage
// error; 3 measured, but at least one result is flagged invalid (too many
// chunks were disturbed by the scheduler); 77 implementation unavailable.
#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#if defined(__APPLE__) || defined(__linux__)
#include <sys/utsname.h>
#endif

#include "bench_build_info.h"
#include "bench_git_revision.h"
#include "counters.h"
#include "implementations.h"
#include "kernel.h"
#include "overhead.h"
#include "workloads.h"

using namespace shewchuk_test;
using namespace shewchuk_bench;

namespace {

const int EXIT_CHECKSUM = 1, EXIT_USAGE = 2, EXIT_INVALID = 3, EXIT_UNAVAILABLE = 77;

// A pass is split into this many chunks (fewer when the pass is tiny).
const long MAX_CHUNKS = 64;
const long MIN_CHUNK_CALLS = 16;
// A result is valid if at least this share of its chunks was kept. Kept
// chunks are clean by construction, so this only guards against a result
// that rests on very few calls.
const double MIN_KEPT_CHUNKS = 0.25;
// A chunk is kept only if the same workload ran undisturbed on the same CPU
// right before it, for at least this many calls (capped at a sixteenth of the
// pass) or this many cycles, whichever comes first. The branch predictor is
// per CPU and needs to see the code for a while to settle: without this rule
// the first chunks of a pass, and the chunks after every migration to
// another core, read 3% to 10% high.
const long WARM_CALLS = 2048;
const std::uint64_t WARM_CYCLES = 2'000'000;
// Warm-up calls right before every chunk (at most a quarter of the chunk).
const long MAX_SLICE_CALLS = 512;
const std::uint64_t WARM_UP_SEED_TAG = 0x5741524D5550ull;

void usage() {
  std::printf(
      "usage: bench_predicates [options]\n"
      "  --impl NAME        implementation to measure (default: reference)\n"
      "  --pred LIST        comma-separated predicates (default: all)\n"
      "  --workload LIST    comma-separated workloads (default: all)\n"
      "  --scale F          multiply the number of calls of every workload by F\n"
      "  --seed N           seed of the input streams (default 0; same seed, same inputs)\n"
      "  --latency          make each call wait for the previous result\n"
      "  --wall-clock       do not use hardware counters (tests the fallback)\n"
      "  --min-pcore-share F  a chunk counts as a performance-core chunk if at least this\n"
      "                     share of its cycles ran there (default 0.999; 0 keeps all)\n"
      "  --out FILE         write the JSON there and a table to stdout\n"
      "                     (default: JSON to stdout)\n"
      "  --smoke            tiny run for CI: --scale 0.002, never exits with status 3\n"
      "  --manifest         no timing: print each workload's measured stage distribution\n"
      "  --list             list implementations, predicates and workloads\n");
}

std::vector<std::string> split(const std::string& list) {
  std::vector<std::string> items;
  std::size_t start = 0;
  while (start <= list.size()) {
    const std::size_t comma = list.find(',', start);
    const std::size_t end = comma == std::string::npos ? list.size() : comma;
    if (end > start) items.push_back(list.substr(start, end - start));
    start = end + 1;
  }
  return items;
}

bool contains(const std::vector<std::string>& items, const char* name) {
  return items.empty() || std::find(items.begin(), items.end(), name) != items.end();
}

std::string json_escape(const std::string& text) {
  std::string out;
  for (const char ch : text) {
    if (ch == '"' || ch == '\\') {
      out += '\\';
      out += ch;
    } else if (static_cast<unsigned char>(ch) < 0x20) {
      out += ' ';
    } else {
      out += ch;
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// Machine description
// ---------------------------------------------------------------------------

struct Machine {
  std::string cpu = "unknown", model = "unknown", os = "unknown", arch = "unknown";
  int num_cpus = 0, performance_cores = 0, efficiency_cores = 0;
  double load_avg[3] = {-1, -1, -1};
};

#if defined(__APPLE__)
std::string sysctl_string(const char* name) {
  char buffer[256];
  std::size_t size = sizeof buffer;
  if (sysctlbyname(name, buffer, &size, nullptr, 0) != 0) return "unknown";
  return std::string(buffer);
}
int sysctl_int(const char* name) {
  int value = 0;
  std::size_t size = sizeof value;
  if (sysctlbyname(name, &value, &size, nullptr, 0) != 0) return 0;
  return value;
}
#endif

Machine describe_machine() {
  Machine machine;
  machine.num_cpus = static_cast<int>(std::thread::hardware_concurrency());
#if defined(__APPLE__)
  machine.cpu = sysctl_string("machdep.cpu.brand_string");
  machine.model = sysctl_string("hw.model");
  machine.os = "macOS " + sysctl_string("kern.osproductversion");
  machine.performance_cores = sysctl_int("hw.perflevel0.physicalcpu");
  machine.efficiency_cores = sysctl_int("hw.perflevel1.physicalcpu");
#elif defined(__linux__)
  if (std::FILE* file = std::fopen("/proc/cpuinfo", "r")) {
    char line[512];
    while (std::fgets(line, sizeof line, file)) {
      if (std::strncmp(line, "model name", 10) == 0) {
        if (const char* colon = std::strchr(line, ':')) {
          machine.cpu = colon + 2;
          while (!machine.cpu.empty() && machine.cpu.back() == '\n') machine.cpu.pop_back();
        }
        break;
      }
    }
    std::fclose(file);
  }
#elif defined(_WIN32)
  machine.os = "Windows";
#endif
#if defined(__APPLE__) || defined(__linux__)
  utsname names;
  if (uname(&names) == 0) {
    machine.arch = names.machine;
#if defined(__linux__)
    machine.os = std::string("Linux ") + names.release;
#endif
  }
  getloadavg(machine.load_avg, 3);
#endif
  return machine;
}

// ---------------------------------------------------------------------------
// Measurement
// ---------------------------------------------------------------------------

struct Chunk {
  std::uint64_t ns = 0, cycles = 0, instructions = 0;
  // Kept: ran entirely on one performance core, after enough warm calls there.
  bool kept = true;
};

struct Result {
  Pred pred;
  const WorkloadSpec* workload;
  long calls = 0, chunk_calls = 0, candidates = 0;
  std::vector<Chunk> chunks;
  long kept_chunks = 0;
  // Per call, over the kept chunks.
  double ns = 0, cycles = 0, instructions = 0;
  double chunk_cv_percent = 0;  // of cycles per call (ns without counters)
  double pcore_share = -1;      // of all cycles of the pass; -1 if unknown
  long stage_counts[NUM_STAGES] = {};
  long long checksum = 0, reference_checksum = 0;
  std::uint64_t input_hash = 0;
  bool checksum_ok = true, valid = true;
};

struct Settings {
  const Implementation* implementation = nullptr;
  bool check_checksum = true;
  Mode mode = Mode::Throughput;
  double scale = 1.0;
  std::uint64_t seed = 0;
  double min_pcore_share = 0.999;
  bool manifest_only = false;
};

// The counters' own cost: what one counters_read() adds between two reads.
CounterSample measure_read_overhead() {
  CounterSample smallest;
  smallest.ns = smallest.cycles = smallest.instructions = UINT64_MAX;
  for (int i = 0; i < 64; i++) {
    const CounterSample before = counters_read();
    const CounterSample after = counters_read();
    smallest.ns = std::min(smallest.ns, after.ns - before.ns);
    smallest.cycles = std::min(smallest.cycles, after.cycles - before.cycles);
    smallest.instructions = std::min(smallest.instructions, after.instructions - before.instructions);
  }
  return smallest;
}

std::uint64_t minus_clamped(std::uint64_t a, std::uint64_t b) { return a > b ? a - b : 0; }

void plan_chunks(const WorkloadSpec& workload, Pred pred, double scale, long* chunks,
                 long* chunk_calls) {
  const long calls = std::max(1L, std::lround(static_cast<double>(workload.calls[pred]) * scale));
  *chunks = std::clamp(calls / MIN_CHUNK_CALLS, 1L, MAX_CHUNKS);
  *chunk_calls = std::max(1L, calls / *chunks);
}

// Runs one (predicate, workload). Returns false with a message on a workload error.
bool measure(const Settings& settings, const CounterInfo& counters,
             const CounterSample& read_overhead, Pred pred, const WorkloadSpec& workload,
             std::vector<double>& buffer, Result* result, std::string* error) {
  long num_chunks, chunk_calls;
  plan_chunks(workload, pred, settings.scale, &num_chunks, &chunk_calls);
  // The buffer holds one chunk, followed by one warm-up slice.
  const std::size_t needed = static_cast<std::size_t>(chunk_calls) * NUM_COORDS[pred];
  if (buffer.size() < 2 * needed) buffer.resize(2 * needed);

  result->pred = pred;
  result->workload = &workload;
  result->chunk_calls = chunk_calls;

  WorkloadStream stream(workload, pred, settings.seed);
  if (!stream.ok() || !stream.prepare(static_cast<std::size_t>(num_chunks * chunk_calls))) {
    *error = stream.error();
    return false;
  }
  result->calls = stream.cases();
  result->candidates = stream.candidates();
  std::copy(stream.stage_counts(), stream.stage_counts() + NUM_STAGES, result->stage_counts);
  result->reference_checksum = stream.reference_checksum();
  result->input_hash = stream.input_hash();
  if (settings.manifest_only) return true;

  // Warm-up inputs come from a different stream (no timed input is ever seen
  // twice): one long warm-up before the pass, and a short slice right before
  // every chunk. Producing a chunk takes longer than timing it, and on a busy
  // machine other work runs on this core meanwhile; the slice brings the
  // caches and the branch predictor back to the steady state being measured.
  const long warm_calls = std::min(WARM_CALLS, std::max(1L, num_chunks * chunk_calls / 16));
  const long slice_calls = std::clamp(chunk_calls / 4, 1L, MAX_SLICE_CALLS);
  double* const slice = buffer.data() + needed;
  WorkloadStream warm_up(workload, pred, settings.seed ^ WARM_UP_SEED_TAG);
  if (!warm_up.ok() ||
      !warm_up.prepare(static_cast<std::size_t>(warm_calls + num_chunks * slice_calls))) {
    *error = warm_up.error();
    return false;
  }

  // How much of this workload has run on the current CPU without disturbance.
  struct {
    int cpu = -1;
    long calls = 0;
    std::uint64_t cycles = 0;
  } warmth;
  std::uint64_t total_cycles = 0, total_pcore_cycles = 0;
  // Runs `count` calls and measures them. Returns true if they were warm
  // enough when they started and ran undisturbed on one performance core.
  auto run = [&](const double* cases, long count, Chunk* chunk, long long* checksum) {
    const int cpu_before = current_cpu();
    const CounterSample before = counters_read();
    *checksum += run_kernel(*settings.implementation, pred, settings.mode, cases,
                            static_cast<std::size_t>(count));
    const CounterSample after = counters_read();
    const int cpu_after = current_cpu();

    bool undisturbed = cpu_after == cpu_before;
    const std::uint64_t cycles = after.cycles - before.cycles;
    chunk->ns = minus_clamped(after.ns - before.ns, read_overhead.ns);
    if (counters.has_cycles) {
      chunk->cycles = minus_clamped(cycles, read_overhead.cycles);
      chunk->instructions =
          minus_clamped(after.instructions - before.instructions, read_overhead.instructions);
      if (counters.has_core_type) {
        const std::uint64_t pcore_cycles = after.pcore_cycles - before.pcore_cycles;
        total_cycles += cycles;
        total_pcore_cycles += pcore_cycles;
        undisturbed = undisturbed && static_cast<double>(pcore_cycles) >=
                                         settings.min_pcore_share * static_cast<double>(cycles);
      }
    }
    if (cpu_before != warmth.cpu) warmth.calls = 0, warmth.cycles = 0;
    const bool was_warm = warmth.calls >= warm_calls ||
                          (counters.has_cycles && warmth.cycles >= WARM_CYCLES);
    // These calls warm the CPU for the next ones.
    warmth.calls = undisturbed ? warmth.calls + count : 0;
    warmth.cycles = undisturbed ? warmth.cycles + cycles : 0;
    warmth.cpu = cpu_after;
    return undisturbed && was_warm;
  };

  Chunk untimed;
  long long untimed_checksum = 0;
  for (long done = 0; done < warm_calls;) {
    const long count = std::min(chunk_calls, warm_calls - done);
    if (!warm_up.fill(buffer.data(), static_cast<std::size_t>(count))) {
      *error = warm_up.error();
      return false;
    }
    run(buffer.data(), count, &untimed, &untimed_checksum);
    done += count;
  }
  for (long i = 0; i < num_chunks; i++) {
    if (!stream.fill(buffer.data(), static_cast<std::size_t>(chunk_calls)) ||
        !warm_up.fill(slice, static_cast<std::size_t>(slice_calls))) {
      *error = stream.ok() ? warm_up.error() : stream.error();
      return false;
    }
    run(slice, slice_calls, &untimed, &untimed_checksum);
    Chunk chunk;
    chunk.kept = run(buffer.data(), chunk_calls, &chunk, &result->checksum);
    result->chunks.push_back(chunk);
  }

  // Totals over the kept chunks. The chunks of a workload are identically
  // distributed, so dropping some does not bias the
  // result. Whole-pass totals, not a trimmed statistic of the chunks:
  // trimming would remove real differences between inputs.
  std::uint64_t ns = 0, cycles = 0, instructions = 0;
  std::vector<double> per_call;
  for (const Chunk& chunk : result->chunks) {
    if (!chunk.kept) continue;
    result->kept_chunks++;
    ns += chunk.ns;
    cycles += chunk.cycles;
    instructions += chunk.instructions;
    per_call.push_back(static_cast<double>(counters.has_cycles ? chunk.cycles : chunk.ns) /
                       static_cast<double>(chunk_calls));
  }
  const double kept_calls = static_cast<double>(result->kept_chunks * chunk_calls);
  if (result->kept_chunks > 0) {
    result->ns = static_cast<double>(ns) / kept_calls;
    result->cycles = static_cast<double>(cycles) / kept_calls;
    result->instructions = static_cast<double>(instructions) / kept_calls;
  }
  if (per_call.size() > 1) {
    double mean = 0, squares = 0;
    for (const double x : per_call) mean += x;
    mean /= static_cast<double>(per_call.size());
    for (const double x : per_call) squares += (x - mean) * (x - mean);
    if (mean > 0) {
      result->chunk_cv_percent =
          100.0 * std::sqrt(squares / static_cast<double>(per_call.size() - 1)) / mean;
    }
  }
  if (counters.has_core_type && total_cycles > 0) {
    result->pcore_share = static_cast<double>(total_pcore_cycles) / static_cast<double>(total_cycles);
  }
  result->valid = static_cast<double>(result->kept_chunks) >=
                  MIN_KEPT_CHUNKS * static_cast<double>(result->chunks.size());
  result->checksum_ok = !settings.check_checksum || result->checksum == result->reference_checksum;
  return true;
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

std::string benchmark_name(const Settings& settings, const Result& result) {
  return std::string(settings.implementation->name) + "/" + PRED_NAME[result.pred] + "/" +
         result.workload->name;
}

void print_stage_shares(std::FILE* out, const Result& result, const char* separator) {
  for (int s = 0; s < NUM_STAGES; s++) {
    std::fprintf(out, "%s%5.1f", s ? separator : "",
                 100.0 * static_cast<double>(result.stage_counts[s]) /
                     static_cast<double>(std::max(1L, result.calls)));
  }
}

void print_table(const Settings& settings, const CounterInfo& counters,
                 const std::vector<Result>& results) {
  std::printf("%-40s %9s %10s %10s %10s %5s %7s %7s   stage shares %%: A0 A B Bx C D\n",
              "benchmark", "calls", "cycles", "instr", "ns", "IPC", "chunkCV", "kept");
  for (const Result& result : results) {
    std::printf("%-40s %9ld", benchmark_name(settings, result).c_str(), result.calls);
    if (counters.has_cycles) {
      std::printf(" %10.2f %10.2f %10.3f %5.2f", result.cycles, result.instructions, result.ns,
                  result.cycles > 0 ? result.instructions / result.cycles : 0.0);
    } else {
      std::printf(" %10s %10s %10.3f %5s", "-", "-", result.ns, "-");
    }
    std::printf(" %6.2f%% %3ld/%-3zu   ", result.chunk_cv_percent, result.kept_chunks,
                result.chunks.size());
    print_stage_shares(stdout, result, " ");
    std::printf("%s%s\n", result.valid ? "" : "  INVALID",
                result.checksum_ok ? "" : "  CHECKSUM MISMATCH");
  }
  if (!counters.has_cycles) {
    std::printf("No cycle counter (%s): WALL-CLOCK nanoseconds only.\n",
                counters.fallback_reason.c_str());
  }
}

void print_manifest(const std::vector<Result>& results) {
  std::printf("| predicate | workload | calls | candidates per call | A0 | A | B | Bx | C | D |\n");
  std::printf("|---|---|--:|--:|--:|--:|--:|--:|--:|--:|\n");
  for (const Result& result : results) {
    std::printf("| %s | `%s` | %ld | %.2f | ", PRED_NAME[result.pred], result.workload->name,
                result.calls,
                static_cast<double>(result.candidates) / static_cast<double>(std::max(1L, result.calls)));
    print_stage_shares(stdout, result, " | ");
    std::printf(" |\n");
  }
}

void write_json(std::FILE* out, const Settings& settings, const CounterInfo& counters,
                const CounterSample& read_overhead, const Machine& machine,
                const std::string& qos, const std::string& command_line,
                const std::vector<Result>& results) {
  char date[32] = "unknown";
  const std::time_t now = std::time(nullptr);
  std::tm utc;
#if defined(_WIN32)
  const bool have_utc = gmtime_s(&utc, &now) == 0;
#else
  const bool have_utc = gmtime_r(&now, &utc) != nullptr;
#endif
  if (have_utc) std::strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%SZ", &utc);

  std::fprintf(out, "{\n  \"context\": {\n");
  std::fprintf(out, "    \"date\": \"%s\",\n", date);
  std::fprintf(out, "    \"command\": \"%s\",\n", json_escape(command_line).c_str());
  std::fprintf(out, "    \"num_cpus\": %d,\n", machine.num_cpus);
  std::fprintf(out, "    \"cpu\": \"%s\",\n", json_escape(machine.cpu).c_str());
  std::fprintf(out, "    \"hardware_model\": \"%s\",\n", json_escape(machine.model).c_str());
  std::fprintf(out, "    \"performance_cores\": %d,\n", machine.performance_cores);
  std::fprintf(out, "    \"efficiency_cores\": %d,\n", machine.efficiency_cores);
  std::fprintf(out, "    \"os\": \"%s\",\n", json_escape(machine.os).c_str());
  std::fprintf(out, "    \"arch\": \"%s\",\n", json_escape(machine.arch).c_str());
  std::fprintf(out, "    \"load_avg\": [%.2f, %.2f, %.2f],\n", machine.load_avg[0],
               machine.load_avg[1], machine.load_avg[2]);
  std::fprintf(out, "    \"library_build_type\": \"%s\",\n", json_escape(SHEWCHUK_BENCH_BUILD_TYPE).c_str());
  std::fprintf(out, "    \"c_compiler\": \"%s\",\n", json_escape(SHEWCHUK_BENCH_C_COMPILER).c_str());
  std::fprintf(out, "    \"c_flags\": \"%s\",\n", json_escape(SHEWCHUK_BENCH_C_FLAGS).c_str());
  std::fprintf(out, "    \"cxx_compiler\": \"%s\",\n", json_escape(SHEWCHUK_BENCH_CXX_COMPILER).c_str());
  std::fprintf(out, "    \"cxx_flags\": \"%s\",\n", json_escape(SHEWCHUK_BENCH_CXX_FLAGS).c_str());
  std::fprintf(out, "    \"strict_fp_flags\": \"%s\",\n", json_escape(SHEWCHUK_BENCH_STRICT_FP_FLAGS).c_str());
  std::fprintf(out, "    \"git_revision\": \"%s\",\n", json_escape(SHEWCHUK_BENCH_GIT_REVISION).c_str());
  std::fprintf(out, "    \"git_dirty\": %s,\n", SHEWCHUK_BENCH_GIT_DIRTY ? "true" : "false");
  std::fprintf(out, "    \"counters\": \"%s\",\n", counters.backend.c_str());
  std::fprintf(out, "    \"counters_fallback_reason\": \"%s\",\n",
               json_escape(counters.fallback_reason).c_str());
  std::fprintf(out, "    \"primary_metric\": \"%s\",\n", counters.has_cycles ? "cycles" : "wall-clock ns");
  std::fprintf(out, "    \"core_type_filter\": %s,\n", counters.has_core_type ? "true" : "false");
  std::fprintf(out, "    \"min_pcore_share\": %.4f,\n", settings.min_pcore_share);
  std::fprintf(out, "    \"scheduling_request\": \"%s\",\n", qos.c_str());
  std::fprintf(out,
               "    \"read_overhead\": {\"cycles\": %" PRIu64 ", \"instructions\": %" PRIu64
               ", \"ns\": %" PRIu64 "},\n",
               counters.has_cycles ? read_overhead.cycles : 0,
               counters.has_cycles ? read_overhead.instructions : 0, read_overhead.ns);
  std::fprintf(out, "    \"implementation\": \"%s\",\n", settings.implementation->name);
  std::fprintf(out, "    \"implementation_description\": \"%s\",\n",
               json_escape(settings.implementation->description).c_str());
  std::fprintf(out, "    \"scenario\": \"library call (function pointer into another translation unit)\",\n");
  std::fprintf(out, "    \"mode\": \"%s\",\n", settings.mode == Mode::Latency ? "latency" : "throughput");
  std::fprintf(out, "    \"protocol\": \"single pass over never-repeated inputs, one process\",\n");
  std::fprintf(out, "    \"seed\": %" PRIu64 ",\n", settings.seed);
  std::fprintf(out, "    \"scale\": %g\n  },\n", settings.scale);

  // The workload manifest: definition and measured stage distribution.
  std::fprintf(out, "  \"workloads\": [\n");
  for (std::size_t i = 0; i < results.size(); i++) {
    const Result& result = results[i];
    const WorkloadSpec& workload = *result.workload;
    std::fprintf(out, "    {\"predicate\": \"%s\", \"workload\": \"%s\", \"description\": \"%s\", ",
                 PRED_NAME[result.pred], workload.name, json_escape(workload.description).c_str());
    std::fprintf(out, "\"stage_filter\": %s%s%s, \"components\": [",
                 workload.stage_filter < 0 ? "" : "\"",
                 workload.stage_filter < 0 ? "null" : STAGE_NAME[workload.stage_filter],
                 workload.stage_filter < 0 ? "" : "\"");
    for (std::size_t c = 0; c < workload.components.size(); c++) {
      std::fprintf(out, "%s{\"generator\": \"%s/%s\", \"probability\": %g}", c ? ", " : "",
                   PRED_NAME[result.pred], workload.components[c].family,
                   workload.components[c].probability);
    }
    std::fprintf(out, "], \"calls\": %ld, \"candidates\": %ld, \"input_hash\": \"%016" PRIx64 "\", ",
                 result.calls, result.candidates, result.input_hash);
    std::fprintf(out, "\"stage_counts\": {");
    for (int s = 0; s < NUM_STAGES; s++) {
      std::fprintf(out, "%s\"%s\": %ld", s ? ", " : "", STAGE_NAME[s], result.stage_counts[s]);
    }
    std::fprintf(out, "}}%s\n", i + 1 < results.size() ? "," : "");
  }
  std::fprintf(out, "  ],\n");

  std::fprintf(out, "  \"benchmarks\": [\n");
  for (std::size_t i = 0; i < results.size(); i++) {
    const Result& result = results[i];
    const std::string name = benchmark_name(settings, result);
    std::fprintf(out, "    {\n      \"name\": \"%s\",\n      \"run_name\": \"%s\",\n", name.c_str(),
                 name.c_str());
    std::fprintf(out, "      \"run_type\": \"iteration\",\n      \"repetitions\": 1,\n"
                      "      \"repetition_index\": 0,\n      \"threads\": 1,\n");
    std::fprintf(out, "      \"iterations\": %ld,\n", result.kept_chunks * result.chunk_calls);
    std::fprintf(out, "      \"real_time\": %.6f,\n      \"cpu_time\": %.6f,\n      \"time_unit\": \"ns\",\n",
                 result.ns, result.ns);
    if (counters.has_cycles) {
      std::fprintf(out, "      \"cycles\": %.6f,\n      \"instructions\": %.6f,\n", result.cycles,
                   result.instructions);
    }
    std::fprintf(out, "      \"implementation\": \"%s\",\n      \"predicate\": \"%s\",\n"
                      "      \"workload\": \"%s\",\n",
                 settings.implementation->name, PRED_NAME[result.pred], result.workload->name);
    std::fprintf(out, "      \"calls\": %ld,\n      \"chunk_calls\": %ld,\n      \"chunks\": %zu,\n"
                      "      \"chunks_kept\": %ld,\n",
                 result.calls, result.chunk_calls, result.chunks.size(), result.kept_chunks);
    std::fprintf(out, "      \"chunk_cv_percent\": %.4f,\n", result.chunk_cv_percent);
    if (result.pcore_share >= 0) std::fprintf(out, "      \"pcore_share\": %.6f,\n", result.pcore_share);
    std::fprintf(out, "      \"checksum\": %lld,\n      \"reference_checksum\": %lld,\n"
                      "      \"checksum_ok\": %s,\n      \"valid\": %s,\n",
                 result.checksum, result.reference_checksum, result.checksum_ok ? "true" : "false",
                 result.valid ? "true" : "false");
    std::fprintf(out, "      \"input_hash\": \"%016" PRIx64 "\",\n", result.input_hash);
    // Raw per-chunk totals, for paired comparisons between two runs.
    std::fprintf(out, "      \"chunk_ns\": [");
    for (std::size_t c = 0; c < result.chunks.size(); c++) {
      std::fprintf(out, "%s%" PRIu64, c ? ", " : "", result.chunks[c].ns);
    }
    std::fprintf(out, "],\n");
    if (counters.has_cycles) {
      std::fprintf(out, "      \"chunk_cycles\": [");
      for (std::size_t c = 0; c < result.chunks.size(); c++) {
        std::fprintf(out, "%s%" PRIu64, c ? ", " : "", result.chunks[c].cycles);
      }
      std::fprintf(out, "],\n      \"chunk_instructions\": [");
      for (std::size_t c = 0; c < result.chunks.size(); c++) {
        std::fprintf(out, "%s%" PRIu64, c ? ", " : "", result.chunks[c].instructions);
      }
      std::fprintf(out, "],\n");
    }
    std::fprintf(out, "      \"chunk_kept\": [");
    for (std::size_t c = 0; c < result.chunks.size(); c++) {
      std::fprintf(out, "%s%s", c ? ", " : "", result.chunks[c].kept ? "true" : "false");
    }
    std::fprintf(out, "]\n    }%s\n", i + 1 < results.size() ? "," : "");
  }
  std::fprintf(out, "  ]\n}\n");
}

const Implementation* find_bench_implementation(const char* name) {
  if (std::strcmp(name, overhead_implementation().name) == 0) return &overhead_implementation();
  return find_implementation(name);
}

void list_everything() {
  std::printf("implementations:\n");
  for (const Implementation& implementation : all_implementations()) {
    const char* reason = implementation.unavailable_reason();
    std::printf("  %-22s %s%s%s\n", implementation.name, implementation.description,
                reason ? " [unavailable: " : "", reason ? reason : "");
    if (reason) std::printf("  %-22s ]\n", "");
  }
  std::printf("  %-22s %s\n", overhead_implementation().name, overhead_implementation().description);
  std::printf("predicates:\n ");
  for (int p = 0; p < NUM_PREDS; p++) std::printf(" %s", PRED_NAME[p]);
  std::printf("\nworkloads (calls at scale 1 for orient2d, orient3d, incircle, insphere):\n");
  for (const WorkloadSpec& workload : all_workloads()) {
    std::printf("  %-20s %8ld %8ld %8ld %8ld  %s\n", workload.name, workload.calls[0],
                workload.calls[1], workload.calls[2], workload.calls[3], workload.description);
  }
}

}  // namespace

int main(int argc, char** argv) {
  const char* impl_name = "reference";
  const char* out_path = nullptr;
  std::vector<std::string> preds, workloads;
  Settings settings;
  bool force_wall_clock = false, smoke = false;

  std::string command_line = "bench_predicates";
  for (int i = 1; i < argc; i++) command_line += std::string(" ") + argv[i];

  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (arg == "--impl" && has_value) {
      impl_name = argv[++i];
    } else if (arg == "--pred" && has_value) {
      preds = split(argv[++i]);
    } else if (arg == "--workload" && has_value) {
      workloads = split(argv[++i]);
    } else if (arg == "--scale" && has_value) {
      settings.scale = std::atof(argv[++i]);
    } else if (arg == "--seed" && has_value) {
      settings.seed = std::strtoull(argv[++i], nullptr, 10);
    } else if (arg == "--min-pcore-share" && has_value) {
      settings.min_pcore_share = std::atof(argv[++i]);
    } else if (arg == "--out" && has_value) {
      out_path = argv[++i];
    } else if (arg == "--latency") {
      settings.mode = Mode::Latency;
    } else if (arg == "--wall-clock") {
      force_wall_clock = true;
    } else if (arg == "--smoke") {
      smoke = true;
      settings.scale = 0.002;
    } else if (arg == "--manifest") {
      settings.manifest_only = true;
    } else if (arg == "--list") {
      list_everything();
      return 0;
    } else {
      usage();
      return EXIT_USAGE;
    }
  }
  if (!(settings.scale > 0.0)) {
    std::fprintf(stderr, "--scale must be positive\n");
    return EXIT_USAGE;
  }

  // Unknown names are errors, not empty selections.
  for (const std::string& name : preds) {
    bool known = false;
    for (int p = 0; p < NUM_PREDS; p++) known = known || name == PRED_NAME[p];
    if (!known) {
      std::fprintf(stderr, "unknown predicate '%s'\n", name.c_str());
      return EXIT_USAGE;
    }
  }
  for (const std::string& name : workloads) {
    bool known = false;
    for (const WorkloadSpec& workload : all_workloads()) known = known || name == workload.name;
    if (!known) {
      std::fprintf(stderr, "unknown workload '%s' (see --list)\n", name.c_str());
      return EXIT_USAGE;
    }
  }
  settings.implementation = find_bench_implementation(impl_name);
  if (!settings.implementation) {
    std::fprintf(stderr, "unknown implementation '%s' (see --list)\n", impl_name);
    return EXIT_USAGE;
  }
  if (const char* reason = settings.implementation->unavailable_reason()) {
    std::fprintf(stderr, "SKIPPED: implementation '%s' is unavailable: %s\n", impl_name, reason);
    return EXIT_UNAVAILABLE;
  }
  settings.check_checksum = settings.implementation != &overhead_implementation();

  initialize_implementations();
  const std::string qos = request_performance_cores();
  const CounterInfo& counters = counters_init(force_wall_clock);
  const bool settled = wait_for_performance_core(settings.min_pcore_share);
  if (!settled) {
    std::fprintf(stderr, "WARNING: this process did not reach a performance core in its "
                         "warm-up; expect chunks to be discarded\n");
  }
  const CounterSample read_overhead = measure_read_overhead();
  const Machine machine = describe_machine();

  std::vector<Result> results;
  std::vector<double> buffer;  // one chunk of inputs, reused for every chunk
  for (int p = 0; p < NUM_PREDS; p++) {
    if (!contains(preds, PRED_NAME[p])) continue;
    for (const WorkloadSpec& workload : all_workloads()) {
      if (workload.calls[p] == 0 || !contains(workloads, workload.name)) continue;
      Result result;
      std::string error;
      if (!measure(settings, counters, read_overhead, static_cast<Pred>(p), workload, buffer,
                   &result, &error)) {
        std::fprintf(stderr, "ERROR: %s/%s: %s\n", PRED_NAME[p], workload.name, error.c_str());
        return EXIT_CHECKSUM;
      }
      results.push_back(std::move(result));
    }
  }
  if (results.empty()) {
    std::fprintf(stderr, "nothing selected: no chosen workload is defined for a chosen predicate\n");
    return EXIT_USAGE;
  }

  if (settings.manifest_only) {
    print_manifest(results);
    return 0;
  }

  if (out_path) {
    std::FILE* out = std::fopen(out_path, "w");
    if (!out) {
      std::fprintf(stderr, "cannot write %s\n", out_path);
      return EXIT_USAGE;
    }
    write_json(out, settings, counters, read_overhead, machine, qos, command_line, results);
    std::fclose(out);
    print_table(settings, counters, results);
  } else {
    write_json(stdout, settings, counters, read_overhead, machine, qos, command_line, results);
  }

  bool all_valid = true, all_checksums = true;
  for (const Result& result : results) {
    all_valid = all_valid && result.valid;
    if (!result.checksum_ok) {
      all_checksums = false;
      std::fprintf(stderr, "CHECKSUM MISMATCH: %s returned sign sum %lld, the reference %lld\n",
                   benchmark_name(settings, result).c_str(), result.checksum,
                   result.reference_checksum);
    }
  }
  if (!all_checksums) return EXIT_CHECKSUM;
  if (!all_valid) {
    std::fprintf(stderr,
                 "INVALID: at least one result kept fewer than %.0f%% of its chunks (the process was "
                 "moved between cores: machine too busy?)%s\n",
                 100.0 * MIN_KEPT_CHUNKS, smoke ? "; ignored by --smoke" : "");
    if (!smoke) return EXIT_INVALID;
  }
  return 0;
}
