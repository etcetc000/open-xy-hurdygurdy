#pragma once
#include "core.h"

namespace hurdygurdy {
void drone_tone(DroneTone&, i32* out, const i32* bow, u32 tonic, u32 role,
                i32 rosin, i32 wheel);
}
