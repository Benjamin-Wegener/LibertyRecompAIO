#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include "gta4_init.h"

namespace {
uint32_t ReadBE32(const uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, sizeof(v));
  return __builtin_bswap32(v);
}
}  // namespace

// PAL diagnostic: find code pointers in the image that sit next to registered
// functions (vtables, callback tables) but have no registered function.
extern "C" void sub_829A7D78(PPCContext& ctx, uint8_t* base) {
  static bool scanned = false;
  if (!scanned) {
    scanned = true;
    auto* runtime = rex::Runtime::instance();
    auto* dispatcher = runtime ? runtime->function_dispatcher() : nullptr;
    if (dispatcher) {
      constexpr uint32_t kCodeLo = 0x82120000, kCodeHi = 0x82A13D5C;
      constexpr uint32_t kImgLo = 0x82000000, kImgHi = 0x831F0000;
      auto reg = [&](uint32_t t) {
        return t >= kCodeLo && t < kCodeHi && (t & 3) == 0 && dispatcher->GetFunction(t) != nullptr;
      };
      auto codeptr = [&](uint32_t t) { return t >= kCodeLo && t < kCodeHi && (t & 3) == 0; };
      std::vector<uint32_t> missing;
      for (uint32_t a = kImgLo + 4; a < kImgHi - 4; a += 4) {
        if (a >= kCodeLo && a < kCodeHi) continue;
        const uint32_t t = ReadBE32(base, a);
        if (!codeptr(t) || dispatcher->GetFunction(t)) continue;
        missing.push_back(t);
      }
      std::sort(missing.begin(), missing.end());
      missing.erase(std::unique(missing.begin(), missing.end()), missing.end());
      REXLOG_WARN("PAL ptrscan: {} unique unregistered code pointers", missing.size());
      std::string line;
      uint32_t n = 0;
      for (uint32_t m : missing) {
        line += fmt::format(" {:08X}", m);
        if (++n % 8 == 0) { REXLOG_WARN("PAL ptrscan:{}", line); line.clear(); }
      }
      if (!line.empty()) REXLOG_WARN("PAL ptrscan:{}", line);
    }
  }
  __imp__sub_829A7D78(ctx, base);
}
