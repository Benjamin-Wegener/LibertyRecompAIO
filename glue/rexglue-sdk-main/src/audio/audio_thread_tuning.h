#pragma once

// Android: keep the guest audio mixer and the XMA decoder off the little cores.
// A little core (A520 at ~2 GHz) needs most of a 5.3 ms audio block to mix it,
// so any hiccup becomes a gap. The cores whose maximum frequency is at least
// 80 % of the fastest core's are the ones the thread may run on.
#if defined(__ANDROID__)
#include <sched.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>

#include <android/log.h>

namespace rex::audio {

inline void PinCallingThreadToBigCores(const char* who) {
  long max_freq[32] = {};
  long best = 0;
  int count = 0;
  for (int cpu = 0; cpu < 32; ++cpu) {
    char path[96];
    std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
    FILE* file = std::fopen(path, "r");
    if (!file) break;
    long value = 0;
    if (std::fscanf(file, "%ld", &value) == 1) max_freq[cpu] = value;
    std::fclose(file);
    if (max_freq[cpu] > best) best = max_freq[cpu];
    ++count;
  }
  if (!count || !best) return;
  cpu_set_t set;
  CPU_ZERO(&set);
  int allowed = 0;
  for (int cpu = 0; cpu < count; ++cpu) {
    if (max_freq[cpu] * 10 >= best * 8) {
      CPU_SET(cpu, &set);
      ++allowed;
    }
  }
  if (allowed == count || !allowed) return;  // all cores alike: nothing to do
  const int result = sched_setaffinity(0, sizeof(set), &set);
  __android_log_print(ANDROID_LOG_WARN, "LibertyRecomp",
                      "audio-affinity %s tid=%d big-cores=%d/%d result=%d", who, int(gettid()),
                      allowed, count, result);
}

}  // namespace rex::audio
#else
namespace rex::audio {
inline void PinCallingThreadToBigCores(const char*) {}
}  // namespace rex::audio
#endif
