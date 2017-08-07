/* -*- mode: c++ -*-
 * Kaleidoscope-LED-Wavepool
 * Copyright (C) 2017 Selene Scriven
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <Kaleidoscope-LED-Wavepool.h>
#include <LEDUtils.h>

namespace kaleidoscope {

#define MS_PER_FRAME_POW2 5  // one frame every 32 ms

int8_t WavepoolEffect::surface[2][WP_WID*WP_HGT];
uint8_t WavepoolEffect::page = 0;
uint8_t WavepoolEffect::frames_since_event = 0;
uint16_t WavepoolEffect::idle_timeout = 5000;  // 5 seconds
/* unused
// map geometric space (14x5) into native keyboard coordinates (16x4)
PROGMEM const uint8_t WavepoolEffect::positions[WP_HGT*WP_WID] = {
     0,  1,  2,  3,  4,  5,  6,    9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 64,   64, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 22,   25, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 37,   40, 58, 59, 60, 61, 62, 63,
    64, 64,  7, 23, 39, 55, 54,   57, 56, 40, 24,  8, 64, 64,
};
*/
// map native keyboard coordinates (16x4) into geometric space (30x12)
PROGMEM const uint16_t WavepoolEffect::rc2pos[ROWS*COLS] = {
     31,  33,  35,  37,  39,  41,  43,      277, 291,       45,  47,  49,  51,  53,  55,  57,
     91,  93,  95,  97,  99, 101, 163,      279, 289,      165, 107, 109, 111, 113, 115, 117,
    151, 153, 155, 157, 159, 161, 223,      281, 287,      225, 167, 169, 171, 173, 175, 177,
    211, 213, 215, 217, 219, 221,      275, 283, 285, 293,      227, 229, 231, 233, 235, 237,
};
//#define RC2POS(x,y) pgm_read_byte(rc2pos + ((uint16_t)(y*2)*WP_WID)+((uint16_t)x*2))
#define RC2POS(x,y) ((uint16_t)pgm_read_word(rc2pos + (y*WP_WID)+x))

WavepoolEffect::WavepoolEffect(void) {
}

void WavepoolEffect::begin(void) {
  event_handler_hook_use(eventHandlerHook);
  LEDMode::begin();
}

Key WavepoolEffect::eventHandlerHook(Key mapped_key, byte row, byte col, uint8_t key_state) {
  if (row >= ROWS || col >= COLS)
    return mapped_key;

  if (keyIsPressed(key_state)) {
    surface[page][RC2POS(col,row)] = 0x7f;
    frames_since_event = 0;
  }

  return mapped_key;
}

void WavepoolEffect::raindrop(uint8_t x, uint8_t y, int8_t *page) {
  uint16_t rainspot = (y*WP_WID) + x;

  page[rainspot] = 0x7f;
}

// this is a lot smaller than the standard library's rand(),
// and still looks random-ish
uint8_t WavepoolEffect::wp_rand() {
    static uint16_t offset = 0x400;
    offset = ((offset + 1) & 0x4fff) | 0x400;
    return (millis()>>MS_PER_FRAME_POW2) + pgm_read_byte(offset);
}

void WavepoolEffect::update(void) {

  // limit the frame rate; one frame every 64 ms
  static uint8_t prev_time = 0;
  uint8_t now = (millis()>>MS_PER_FRAME_POW2) % 0xff;
  if (now != prev_time) {
      prev_time = now;
  } else {
      return;
  }

  // rotate the colors over time
  static uint8_t current_hue = 0;
  current_hue ++;

  frames_since_event ++;

  // needs two pages of height map to do the calculations
  int8_t *newpg = &surface[page^1][0];
  int8_t *oldpg = &surface[page][0];

  // rain a bit while idle
  static uint8_t frames_till_next_drop = 0;
  static int8_t prev_x = -1;
  static int8_t prev_y = -1;
  if (idle_timeout > 0) {
    // repeat previous raindrop to give it a slightly better effect
    if (prev_x >= 0) {
      raindrop(prev_x, prev_y, oldpg);
      prev_x = prev_y = -1;
    }
    if (frames_since_event
            >= (frames_till_next_drop
                + (idle_timeout >> MS_PER_FRAME_POW2))) {
        frames_till_next_drop = 4 + (wp_rand() & 0x3f);
        frames_since_event = idle_timeout >> MS_PER_FRAME_POW2;

        uint8_t x = 1 + wp_rand() % (WP_WID-2);
        uint8_t y = 1 + wp_rand() % (WP_HGT-2);
        raindrop(x, y, oldpg);

        prev_x = x;
        prev_y = y;
    }
  }

  // calculate water movement
  int8_t offsets[] = { -WP_WID,    WP_WID,
                              -1,         1,
                       -WP_WID-1, -WP_WID+1,
                        WP_WID-1,  WP_WID+1
                     };
  for (uint8_t y = 1; y < WP_HGT-1; y++) {
    for (uint8_t x = 1; x < WP_WID-1; x++) {
      uint16_t offset = ((uint16_t)y*WP_WID) + x;

      int16_t value;

      // add up all samples, divide, subtract prev frame's center
      int8_t *p;
      for(p=offsets, value=oldpg[offset]; p<offsets+8; p++)
          value += (int16_t)(oldpg[offset + (*p)]);
      value = (value >> 2) - (int16_t)(newpg[offset]);

      // reduce intensity gradually over time
      value = value - (value >> 3);
      #define VMAX 80  // fudge factor, try to prevent clipping
      if (value < -VMAX) value = -VMAX;
      else if (value > VMAX) value = VMAX;
      newpg[offset] = value;
    }
  }

  // draw the water on the keys
  for (byte r = 0; r < ROWS; r++) {
    for (byte c = 0; c < COLS; c++) {
      int8_t height = oldpg[RC2POS(c,r)];

      uint8_t intensity = abs(height) * 2;

      // color starts white but gets dimmer and more saturated as it fades,
      // with hue wobbling according to height map
      int16_t hue = (current_hue + (int16_t)height + (height>>1)) & 0xff;

      cRGB color = hsvToRgb(hue,
                            0xff - intensity,
                            ((uint16_t)intensity)*2);

      LEDControl.setCrgbAt(r, c, color);
    }
  }

  // swap pages every frame
  page ^= 1;

}

}

kaleidoscope::WavepoolEffect WavepoolEffect;
