#include "counters.h"

#include <chrono>
#include <cstring>

#if defined(__APPLE__)
#include <libproc.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <unistd.h>
#elif defined(__linux__)
#include <cerrno>
#include <linux/perf_event.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace shewchuk_bench {
namespace {

CounterInfo g_info;

std::uint64_t now_ns() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
}

#if defined(__APPLE__) && defined(RUSAGE_INFO_V6)
#define SHEWCHUK_BENCH_HAVE_HARDWARE_COUNTERS 1
const char* const HARDWARE_BACKEND = "macos-proc_pid_rusage";
pid_t g_pid;

bool hardware_open(std::string* why_not) {
  g_pid = getpid();
  rusage_info_v6 info;
  if (proc_pid_rusage(g_pid, RUSAGE_INFO_V6, reinterpret_cast<rusage_info_t*>(&info)) != 0) {
    *why_not = "proc_pid_rusage(RUSAGE_INFO_V6) failed";
    return false;
  }
  return true;
}

void hardware_read(CounterSample* sample) {
  rusage_info_v6 info;
  std::memset(&info, 0, sizeof info);
  proc_pid_rusage(g_pid, RUSAGE_INFO_V6, reinterpret_cast<rusage_info_t*>(&info));
  sample->cycles = info.ri_cycles;
  sample->instructions = info.ri_instructions;
  sample->pcore_cycles = info.ri_pcycles;
}

#elif defined(__linux__)
#define SHEWCHUK_BENCH_HAVE_HARDWARE_COUNTERS 1
const char* const HARDWARE_BACKEND = "linux-perf_event_open";
int g_group_fd = -1;

int open_event(std::uint64_t config, int group_fd) {
  perf_event_attr attr;
  std::memset(&attr, 0, sizeof attr);
  attr.type = PERF_TYPE_HARDWARE;
  attr.size = sizeof attr;
  attr.config = config;
  attr.disabled = group_fd == -1 ? 1 : 0;
  attr.exclude_kernel = 1;
  attr.exclude_hv = 1;
  attr.read_format = PERF_FORMAT_GROUP;
  // This thread, any CPU.
  return static_cast<int>(syscall(SYS_perf_event_open, &attr, 0, -1, group_fd, 0));
}

bool hardware_open(std::string* why_not) {
  g_group_fd = open_event(PERF_COUNT_HW_CPU_CYCLES, -1);
  if (g_group_fd < 0) {
    *why_not = std::string("perf_event_open(cycles) failed: ") + std::strerror(errno);
    return false;
  }
  const int instructions_fd = open_event(PERF_COUNT_HW_INSTRUCTIONS, g_group_fd);
  if (instructions_fd < 0) {
    *why_not = std::string("perf_event_open(instructions) failed: ") + std::strerror(errno);
    close(g_group_fd);
    g_group_fd = -1;
    return false;
  }
  ioctl(g_group_fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
  ioctl(g_group_fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
  return true;
}

void hardware_read(CounterSample* sample) {
  // PERF_FORMAT_GROUP: { nr, value[nr] }, in the order the events were opened.
  std::uint64_t values[3] = {0, 0, 0};
  if (read(g_group_fd, values, sizeof values) == static_cast<ssize_t>(sizeof values)) {
    sample->cycles = values[1];
    sample->instructions = values[2];
  }
}

#else
#define SHEWCHUK_BENCH_HAVE_HARDWARE_COUNTERS 0
#endif

#if SHEWCHUK_BENCH_HAVE_HARDWARE_COUNTERS
// True if the counters advance while this process computes. They read zero on
// some virtual machines even though the calls succeed.
bool hardware_counts() {
  CounterSample before, after;
  hardware_read(&before);
  volatile double sink = 1.0;
  for (int i = 0; i < 200000; i++) sink = sink * 1.0000001 + 1e-9;
  hardware_read(&after);
  return after.cycles > before.cycles && after.instructions > before.instructions;
}
#endif

}  // namespace

const CounterInfo& counters_init(bool force_wall_clock) {
  g_info = CounterInfo{};
  g_info.backend = "wall-clock";
  if (force_wall_clock) {
    g_info.fallback_reason = "requested with --wall-clock";
    return g_info;
  }
#if SHEWCHUK_BENCH_HAVE_HARDWARE_COUNTERS
  std::string why_not;
  if (!hardware_open(&why_not)) {
    g_info.fallback_reason = why_not;
  } else if (!hardware_counts()) {
    g_info.fallback_reason =
        std::string(HARDWARE_BACKEND) + " reads zero cycles here (virtual machine?)";
  } else {
    g_info.backend = HARDWARE_BACKEND;
    g_info.has_cycles = true;
#if defined(__APPLE__)
    // Performance-core cycles are only meaningful on machines with two core
    // types. (Do not infer this from the counter: a new process often starts
    // on an efficiency core and has no performance-core cycles yet.)
    int levels = 0;
    std::size_t size = sizeof levels;
    g_info.has_core_type =
        sysctlbyname("hw.nperflevels", &levels, &size, nullptr, 0) == 0 && levels > 1;
#endif
  }
#else
  g_info.fallback_reason = "no unprivileged cycle counter is implemented for this platform";
#endif
  return g_info;
}

bool wait_for_performance_core(double min_share) {
  if (!g_info.has_core_type) return true;
  // A new process often starts on an efficiency core. Compute until one
  // interval of about a millisecond ran entirely on performance cores.
  for (int attempt = 0; attempt < 500; attempt++) {
    const CounterSample before = counters_read();
    volatile double sink = 1.0;
    for (int i = 0; i < 400000; i++) sink = sink * 1.0000001 + 1e-9;
    const CounterSample after = counters_read();
    const double cycles = static_cast<double>(after.cycles - before.cycles);
    const double pcore_cycles = static_cast<double>(after.pcore_cycles - before.pcore_cycles);
    if (cycles > 0 && pcore_cycles >= min_share * cycles) return true;
  }
  return false;
}

CounterSample counters_read() {
  CounterSample sample;
#if SHEWCHUK_BENCH_HAVE_HARDWARE_COUNTERS
  if (g_info.has_cycles) hardware_read(&sample);
#endif
  sample.ns = now_ns();
  return sample;
}

int current_cpu() {
#if defined(__APPLE__)
  std::size_t cpu = 0;
  return pthread_cpu_number_np(&cpu) == 0 ? static_cast<int>(cpu) : -1;
#elif defined(__linux__)
  return sched_getcpu();
#else
  return -1;
#endif
}

std::string request_performance_cores() {
#if defined(__APPLE__)
  if (pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) == 0) {
    return "QOS_CLASS_USER_INTERACTIVE";
  }
  return "pthread_set_qos_class_self_np failed";
#else
  return "none";
#endif
}

}  // namespace shewchuk_bench
