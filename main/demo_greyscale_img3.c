#include "sdkconfig.h"

#include <string.h>

#include <badge_input.h>
#include <badge_eink_dev.h>
#include <badge_eink_lut.h>
#include <badge_eink_fb.h>
#include <badge_eink.h>

#include "img_hacking.h"

void demoGreyscaleImg3(void) {
  uint32_t *tmpbuf = (uint32_t *) badge_eink_fb;

  // curved
  const uint8_t lvl_buf[128] = {
      0,   4,   9,   13,  17,  22,  26,  30,  34,  38,  42,  45,  49,  53,  57,
      60,  64,  67,  71,  74,  77,  81,  84,  87,  90,  93,  97,  100, 103, 105,
      108, 111, 114, 117, 119, 122, 125, 127, 130, 132, 135, 137, 140, 142, 145,
      147, 149, 151, 154, 156, 158, 160, 162, 164, 166, 168, 170, 172, 174, 176,
      178, 179, 181, 183, 185, 186, 188, 190, 191, 193, 195, 196, 198, 199, 201,
      202, 204, 205, 206, 208, 209, 211, 212, 213, 214, 216, 217, 218, 219, 221,
      222, 223, 224, 225, 226, 227, 228, 230, 231, 232, 233, 234, 235, 236, 237,
      237, 238, 239, 240, 241, 242, 243, 244, 245, 245, 246, 247, 248, 249, 249,
      250, 251, 252, 252, 253, 254, 254, 255 // e=0.15, n=127
  };

  // trying to get rid of all ghosting and end with a black screen.
  int i;
  for (i = 0; i < 3; i++) {
    /* draw initial pattern */
    memset(tmpbuf, (i & 1) ? 0xff : 0x00, DISP_SIZE_X_B * DISP_SIZE_Y);

    struct badge_eink_update eink_upd = {
      .lut      = BADGE_EINK_LUT_DEFAULT,
      .reg_0x3a = 26,   // 26 dummy lines per gate
      .reg_0x3b = 0x08, // 62us per line
      .y_start  = 0,
      .y_end    = 295,
    };
    badge_eink_update(tmpbuf, &eink_upd);
  }

  for (i = 64; i > 0; i >>= 1) {
    int ii = i;
    int p = 8;

    while ((ii & 1) == 0 && (p > 1)) {
      ii >>= 1;
      p >>= 1;
    }

    int j;
    for (j = 0; j < p; j++) {
      int y_start = 0 + j * (DISP_SIZE_Y / p);
      int y_end = y_start + (DISP_SIZE_Y / p) - 1;

      int x, y;
      const uint8_t *ptr = img_hacking;
      uint32_t *dptr = tmpbuf;
      for (y = 0; y < DISP_SIZE_Y; y++) {
        uint32_t res = 0;
        for (x = 0; x < DISP_SIZE_X; x++) {
          res <<= 1;
          uint8_t pixel = *ptr++;
          int j;
          for (j = 0; pixel > lvl_buf[j]; j++)
            ;
          if (y >= y_start && y <= y_end && j & i)
            res++;
          if ((x & 31) == 31)
            *dptr++ = res;
        }
      }

      // LUT:
      //   Ignore old state;
      //   Do nothing when bit is not set;
      //   Make pixel whiter when bit is set;
      //   Duration is <ii> cycles.
      struct badge_eink_lut_entry lut[] = {
        { .length = ii, .voltages = 0x88, },
        { .length = 0 }
      };

      struct badge_eink_update eink_upd = {
        .lut      = BADGE_EINK_LUT_CUSTOM,
        .lut_custom = lut,
        .reg_0x3a = 0,  // no dummy lines per gate
        .reg_0x3b = 0,  // 30us per line
        .y_start  = y_start,
        .y_end    = y_end + 1,
      };
      badge_eink_update(tmpbuf, &eink_upd);
    }
  }

  // wait for random keypress
  badge_input_get_event(-1);
}
