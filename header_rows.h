// Two-row header adapted from Open-XY Macro Osc. See LICENSE.header and PROVENANCE.md.
#pragma once
#include "display.h"
namespace hurdygurdy { namespace header_rows {
const unsigned kPages[2][4] = {{0,1,2,3},{4,5,6,7}};
const char* const kTitles[8] = {"wheel","buzz","body","unison","rosin","coup","drone","tonic"};
const unsigned kBarNode=46, kBarSteps=8;
const unsigned kTitleText=0, kValueText=4, kGreyTitleText=8, kGreyValueText=12;
const opxy::i32 kColumn = 120 << 10, kTitleX = 5 << 10, kValueX = 65 << 10, kBarStep = 60 << 10;
const opxy::i32 kEditedTop[2] = {0, 18 << 10}, kGreyTop[2] = {0, 20 << 10};
const opxy::i32 kTitleY = 10 << 10, kValueY = 17818, kGreyY = 13 << 10;
// The bright values are authored at the header's 20 px and the grey ones at
// 13 px, so each rests at unit scale: 13 px is 666/1024 of 20, and 20 px is
// 1575/1024 of 13.
const opxy::i32 kGrowScale = 666, kShrinkScale = 1575;
// A switch grows the edited row and shrinks the other on an exponential
// ease-out: the distance left halves every 21 ms, so it is within 1% after
// 0.15 s, when it settles.
const opxy::i32 kSettle = 154;            // 0.15 s, Q10
const unsigned kOctavesPerQ10 = 1496;     // Q15 halvings per Q10 second: one per 21 ms

// Round only the screen caption; the parameter keeps its full Q31 precision.
inline void amount(char* out, unsigned capacity, opxy::i32 value) {
    const unsigned p = value < 0 ? 0 : ((static_cast<unsigned>(value) >> 10) * 100 + (1u << 20)) >> 21;
    char text[4];
    unsigned n = 0;
    if (p >= 100) text[n++] = '1';
    if (p >= 10) text[n++] = static_cast<char>('0' + p / 10 % 10);
    text[n++] = static_cast<char>('0' + p % 10);
    text[n] = 0;
    opxy::draw::copy(out, capacity, text);
}

inline unsigned HalvingQ16(unsigned octaves_q15) {
    const unsigned table[9]={65536,60097,55109,50535,46341,42495,38968,35734,32768};
    const unsigned whole=octaves_q15>>15, eighth=(octaves_q15>>12)&7, fraction=octaves_q15&4095;
    return (table[eighth]-(((table[eighth]-table[eighth+1])*fraction)>>12))>>whole;
}
inline void caption(char* out, unsigned capacity, unsigned slot, const opxy::draw::Input& input) {
    const opxy::i32 value=opxy::draw::clamp(input.slots[slot],0,2147483647);
    if(slot==7) {
        const char* const keys[12]={"c","db","d","eb","e","f","gb","g","ab","a","bb","b"};
        unsigned tonic=((static_cast<unsigned>(value)>>16)*12u)>>15;
        if(tonic>11)tonic=11;
        opxy::draw::copy(out,capacity,keys[tonic]);
    } else amount(out,capacity,value);
}
// Between where a switch settles and where it started: `left` (Q16) of the way back.
inline opxy::i32 between(opxy::i32 settled, opxy::i32 start, unsigned left) {
  return settled + ((start - settled) * static_cast<opxy::i32>(left >> 6) >> 10);
}

inline void render(DisplayState& state,const opxy::draw::Input& input,Frame& frame) {
  // The page being edited: page 2 once the first position shows an alternate.
  const unsigned page = input.shown[0] >= 4 ? 1 : 0;
  if (state.header_page && state.track == input.track && state.header_page != page + 1 && !input.reduced_motion)
    state.header_since = 0;
  else if (state.header_since < kSettle)
    state.header_since += opxy::draw::clamp(input.elapsed, 0, kSettle);
  if (input.reduced_motion || state.header_since > kSettle) state.header_since = kSettle;
  state.header_page = page + 1;
  state.track = input.track;
  const unsigned left = state.header_since >= kSettle ? 0 : HalvingQ16(static_cast<unsigned>(state.header_since) * kOctavesPerQ10);
  // The edited page's row grows from grey and the other's shrinks, each in
  // place, and the bar moves between them. The bright text draws the row the
  // bar is nearer, so the two sets trade rows halfway through.
  const unsigned other = 1 - page, bright = left > (1u << 15) ? other : page;
  const opxy::i32 edited = kEditedTop[page], was = kGreyTop[page], grey = kGreyTop[other], from = kEditedTop[other];
  for (unsigned i = 0; i < kBarSteps; ++i)
    opxy::draw::place(frame, kBarNode + i, i * kBarStep, between(edited, from, left), opxy::draw::unit, 0,
                      opxy::draw::keep);
  // The stock header's values too: covered on the device, shown by previews
  // that draw the stock header over the artwork.
  for (unsigned i = 0; i < 4; ++i) caption(frame.header[i], sizeof(frame.header[i]), input.shown[i], input);
  for (unsigned row = 0; row < 2; ++row) {
    const bool growing = row == page;
    const opxy::i32 title_y = growing ? between(edited + kTitleY, was + kGreyY, left)
                                      : between(grey + kGreyY, from + kTitleY, left);
    const opxy::i32 value_y = growing ? between(edited + kValueY, was + kGreyY, left)
                                      : between(grey + kGreyY, from + kValueY, left);
    // The values' size as a fraction of the header's 20 px.
    const opxy::i32 size =
        growing ? between(opxy::draw::unit, kGrowScale, left) : between(kGrowScale, opxy::draw::unit, left);
    const unsigned titles = row == bright ? kTitleText : kGreyTitleText;
    const unsigned values = row == bright ? kValueText : kGreyValueText;
    const opxy::i32 scale = row == bright ? size : size * kShrinkScale >> 10;
    for (unsigned i = 0; i < 4; ++i) {
      const opxy::i32 x = i * kColumn;
      const unsigned slot = kPages[row][i];
      char value[opxy::draw::header_capacity];
      caption(value, sizeof(value), slot, input);
      opxy::draw::text(frame, titles + i, x + kTitleX, title_y, opxy::draw::unit, kTitles[slot]);
      opxy::draw::text(frame, values + i, x + kValueX, value_y, scale, value);
    }
  }
}
}} // namespace hurdygurdy::header_rows
