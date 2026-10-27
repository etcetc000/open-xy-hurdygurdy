#pragma once
#include "core.h"
namespace hurdygurdy {
// Exact fixed-numerator quotients over callback operand ranges.
u32 divide_phase_period(u32 denominator); // 0xffffffff / d, 64 <= d <= 0xffffff
u32 divide_window_weight(u32 denominator); // 0x80000000 / d, 768 <= d <= 64000
u32 detune_step(u32 denominator); // ((65536-d)<<16)/d, 64708 <= d <= 65535
}
