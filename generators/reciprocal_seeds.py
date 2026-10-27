"""Generate lower Q-format reciprocal seeds using exact integer arithmetic."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
values=[(1<<59)//((i+1)<<21) for i in range(256,512)]
lines=['#pragma once','#include "core.h"','namespace hurdygurdy {',
       '// 2^59 divided by each normalized-bin upper endpoint.',
       'const i32 reciprocal_seeds[256] = {']
lines.extend('    '+', '.join(map(str,values[i:i+8]))+',' for i in range(0,256,8))
lines+=['};']
source=(root/'tables.h').read_text()
ratios=[int(x) for x in re.findall(r'\d+',re.search(r'hg_twin_ratio\[33\] = \{(.*?)\};',source,re.S)[1])]
minimum=min(ratios)>>15
if minimum!=64708 or max(ratios)>>15!=65535: raise ValueError('Update the documented detune domain and exhaustive test')
steps=[((65536-d)<<16)//d for d in range(65535,minimum-1,-1)]
if max(steps)>65535: raise ValueError('Detune step no longer fits u16')
lines+=['// Exact quotients for descending detune divisors 65535 through 64708.',
        'const opxy::u16 detune_steps[828] = {']
lines.extend('    '+', '.join(map(str,steps[i:i+16]))+',' for i in range(0,len(steps),16))
lines+=['};','}','']
(root/'reciprocal_seeds.h').write_text('\n'.join(lines))
