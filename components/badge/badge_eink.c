#include <sdkconfig.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <esp_heap_caps.h>
#include <esp_log.h>

#include "badge_pins.h"
#include "badge_eink_dev.h"
#include "badge_eink_lut.h"
#include "badge_eink.h"

static const char *TAG = "badge_eink";

static const uint8_t xlat_curve[256] = {
    0x00,0x01,0x01,0x02,0x02,0x03,0x03,0x03,0x04,0x04,0x05,0x05,
    0x06,0x06,0x07,0x07,0x08,0x08,0x09,0x09,0x0a,0x0a,0x0a,0x0b,
    0x0b,0x0c,0x0c,0x0d,0x0d,0x0e,0x0e,0x0f,0x0f,0x10,0x10,0x11,
    0x11,0x12,0x12,0x13,0x13,0x14,0x15,0x15,0x16,0x16,0x17,0x17,
    0x18,0x18,0x19,0x19,0x1a,0x1a,0x1b,0x1b,0x1c,0x1d,0x1d,0x1e,
    0x1e,0x1f,0x1f,0x20,0x20,0x21,0x22,0x22,0x23,0x23,0x24,0x25,
    0x25,0x26,0x26,0x27,0x27,0x28,0x29,0x29,0x2a,0x2a,0x2b,0x2c,
    0x2c,0x2d,0x2e,0x2e,0x2f,0x2f,0x30,0x31,0x31,0x32,0x33,0x33,
    0x34,0x35,0x35,0x36,0x37,0x37,0x38,0x39,0x39,0x3a,0x3b,0x3b,
    0x3c,0x3d,0x3e,0x3e,0x3f,0x40,0x40,0x41,0x42,0x43,0x43,0x44,
    0x45,0x46,0x46,0x47,0x48,0x49,0x49,0x4a,0x4b,0x4c,0x4c,0x4d,
    0x4e,0x4f,0x50,0x50,0x51,0x52,0x53,0x54,0x55,0x55,0x56,0x57,
    0x58,0x59,0x5a,0x5b,0x5b,0x5c,0x5d,0x5e,0x5f,0x60,0x61,0x62,
    0x63,0x64,0x65,0x66,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,
    0x6e,0x6f,0x70,0x71,0x72,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,
    0x7b,0x7c,0x7d,0x7e,0x80,0x81,0x82,0x83,0x84,0x86,0x87,0x88,
    0x89,0x8a,0x8c,0x8d,0x8e,0x90,0x91,0x92,0x93,0x95,0x96,0x98,
    0x99,0x9a,0x9c,0x9d,0x9f,0xa0,0xa2,0xa3,0xa5,0xa6,0xa8,0xa9,
    0xab,0xac,0xae,0xb0,0xb1,0xb3,0xb5,0xb6,0xb8,0xba,0xbc,0xbe,
    0xbf,0xc1,0xc3,0xc5,0xc7,0xc9,0xcb,0xcd,0xcf,0xd1,0xd3,0xd6,
    0xd8,0xda,0xdc,0xdf,0xe1,0xe3,0xe6,0xe8,0xeb,0xed,0xf0,0xf3,
    0xf5,0xf8,0xfb,0xfe,
};

static uint32_t *badge_eink_tmpbuf = NULL;
#ifdef CONFIG_SHA_BADGE_EINK_DEPG0290B1
static uint32_t *badge_eink_oldbuf = NULL;
#endif // CONFIG_SHA_BADGE_EINK_DEPG0290B1
static bool badge_eink_have_oldbuf = false;

static void
memcpy_u32(uint32_t *dst, uint32_t *src, size_t size)
{
	while (size-- > 0)
	{
		*dst++ = *src++;
	}
}

static void
memset_u32(uint32_t *dst, uint32_t value, size_t size)
{
	while (size-- > 0)
	{
		*dst++ = value;
	}
}

static void
badge_eink_create_bitplane(const uint8_t *img, uint32_t *buf, int bit, int flags)
{
#ifdef EPD_ROTATED_180
	flags ^= DISPLAY_FLAG_ROTATE_180;
#endif
	int x, y;
	int pos, dx, dy;
	if (flags & DISPLAY_FLAG_ROTATE_180)
	{
		pos = DISP_SIZE_Y-1;
		dx = DISP_SIZE_Y;
		dy = -DISP_SIZE_Y*DISP_SIZE_X - 1;
	}
	else
	{
		pos = (DISP_SIZE_X-1)*DISP_SIZE_Y;
		dx = -DISP_SIZE_Y;
		dy = DISP_SIZE_Y*DISP_SIZE_X + 1;
	}
	for (y = 0; y < DISP_SIZE_Y; y++) {
		for (x = 0; x < DISP_SIZE_X;) {
			int x_bits;
			uint32_t res = 0;
			for (x_bits=0; x_bits<32; x_bits++)
			{
				res <<= 1;
				if (flags & DISPLAY_FLAG_GREYSCALE)
				{
					uint8_t pixel = img[pos];
					pos += dx;
					int j = xlat_curve[pixel];
					if ((j & bit) != 0)
						res++;
				}
				else
				{
					uint8_t pixel = img[pos >> 3] >> (pos & 7);
					pos += dx;
					if ((pixel & 1) != 0)
						res++;
				}
				x++;
			}
			*buf++ = res;
		}
		pos += dy;
	}
}

static void
badge_eink_write_bitplane(const uint32_t *buf, int y_start, int y_end)
{
	badge_eink_set_ram_area(0, DISP_SIZE_X_B - 1, 0, DISP_SIZE_Y - 1);
	badge_eink_set_ram_pointer(0, 0);
	badge_eink_dev_write_command_init(0x24);
	int pos;
	for (pos=0; pos < y_start * DISP_SIZE_X_B/4; pos++)
		badge_eink_dev_write_byte_u32(0);
	for (; pos < (y_end+1) * DISP_SIZE_X_B/4; pos++)
		badge_eink_dev_write_byte_u32(buf[pos]);
	for (; pos < DISP_SIZE_Y * DISP_SIZE_X_B/4; pos++)
		badge_eink_dev_write_byte_u32(0);
	badge_eink_dev_write_command_end();
}

const struct badge_eink_update eink_upd_default = {
	.lut      = BADGE_EINK_LUT_DEFAULT,
	.reg_0x3a = 26,   // 26 dummy lines per gate
	.reg_0x3b = 0x08, // 62us per line
	.y_start  = 0,
	.y_end    = 295,
};

void
badge_eink_update(const struct badge_eink_update *upd_conf)
{
	// generate lut data
	const struct badge_eink_lut_entry *lut_entries;

	if (upd_conf->lut == BADGE_EINK_LUT_CUSTOM)
	{
		lut_entries = upd_conf->lut_custom;
	}
	else if (upd_conf->lut >= 0 && upd_conf->lut <= BADGE_EINK_LUT_MAX)
	{
		const struct badge_eink_lut_entry *lut_lookup[BADGE_EINK_LUT_MAX + 1] = {
			badge_eink_lut_full,
			badge_eink_lut_normal,
			badge_eink_lut_faster,
			badge_eink_lut_fastest,
		};
		lut_entries = lut_lookup[upd_conf->lut];
	}
	else
	{
		lut_entries = badge_eink_lut_full;
	}

	uint8_t lut[BADGE_EINK_LUT_MAX_SIZE];
	int lut_len = badge_eink_lut_generate(lut_entries, upd_conf->lut_flags, lut);
	assert( lut_len >= 0 );

	badge_eink_dev_write_command_stream(0x32, lut, lut_len);

#ifdef CONFIG_SHA_BADGE_EINK_DEPG0290B1
	if (badge_eink_have_oldbuf)
		badge_eink_dev_write_command_stream_u32(0x26, badge_eink_oldbuf, DISP_SIZE_X_B * DISP_SIZE_Y/4);
#endif

	// write number of overscan lines
	badge_eink_dev_write_command_p1(0x3a, upd_conf->reg_0x3a);

	// write time to write every line
	badge_eink_dev_write_command_p1(0x3b, upd_conf->reg_0x3b);

	uint16_t y_len = upd_conf->y_end - upd_conf->y_start;
	// configure length of update
	badge_eink_dev_write_command_p3(0x01, y_len & 0xff, y_len >> 8, 0x00);

	// configure starting-line of update
	badge_eink_dev_write_command_p2(0x0f, upd_conf->y_start & 0xff, upd_conf->y_start >> 8);

	// bitmapped enabled phases of the update: (in this order)
	//   80 - enable clock signal
	//   40 - enable CP
	//   20 - load temperature value
	//   10 - load LUT
	//   08 - initial display
	//   04 - pattern display
	//   02 - disable CP
	//   01 - disable clock signal
	badge_eink_dev_write_command_p1(0x22, 0xc7);

	// start update
	badge_eink_dev_write_command(0x20);

#ifdef CONFIG_SHA_BADGE_EINK_DEPG0290B1
	memcpy_u32(badge_eink_oldbuf, badge_eink_tmpbuf, DISP_SIZE_X_B * DISP_SIZE_Y/4);
#endif // CONFIG_SHA_BADGE_EINK_DEPG0290B1
	badge_eink_have_oldbuf = true;
}

void
badge_eink_display_one_layer(const uint8_t *img, int flags)
{
	int lut_mode = 
		(flags >> DISPLAY_FLAG_LUT_BIT) & ((1 << DISPLAY_FLAG_LUT_SIZE)-1);

	uint32_t *buf = badge_eink_tmpbuf;
	if (img == NULL)
	{
		memset_u32(buf, 0, DISP_SIZE_X_B * DISP_SIZE_Y/4);
	}
	else
	{
		badge_eink_create_bitplane(img, buf, 0x80, flags);
	}

	if ((flags & DISPLAY_FLAG_NO_UPDATE) != 0)
	{
		badge_eink_write_bitplane(buf, 0, DISP_SIZE_Y-1);
		return;
	}

	int lut_flags = 0;
	if (badge_eink_have_oldbuf)
	{
		// old image is known; prefer to do a partial update
		if ((flags & DISPLAY_FLAG_FULL_UPDATE) == 0)
			lut_flags |= LUT_FLAG_PARTIAL;
	}
	else
	{
		// old image not known; do full update
		lut_flags |= LUT_FLAG_FIRST;
	}

	badge_eink_write_bitplane(buf, 0, DISP_SIZE_Y-1);
	struct badge_eink_update eink_upd = {
		.lut       = lut_mode > 0 ? lut_mode - 1 : BADGE_EINK_LUT_DEFAULT,
		.lut_flags = lut_flags,
		.reg_0x3a  = 26,   // 26 dummy lines per gate
		.reg_0x3b  = 0x08, // 62us per line
		.y_start   = 0,
		.y_end     = 295,
	};
	badge_eink_update(&eink_upd);
}

void
badge_eink_display(const uint8_t *img, int flags)
{
	// is it a 1 bit per pixel image?
	if ((flags & DISPLAY_FLAG_GREYSCALE) == 0)
	{
		badge_eink_display_one_layer(img, flags);
		return;
	}

	{ // start with black.
		badge_eink_display_one_layer(NULL, (flags | DISPLAY_FLAG_FULL_UPDATE) & ~DISPLAY_FLAG_GREYSCALE);
	}

	int i;
#ifdef CONFIG_SHA_BADGE_EINK_DEPG0290B1
	for (i = 64; i > 2; i >>= 1) {
		int ii = i;
		int p = 2;
#else
	for (i = 64; i > 0; i >>= 1) {
		int ii = i;
		int p = 8;
#endif

		while ((ii & 1) == 0 && (p > 1)) {
			ii >>= 1;
			p >>= 1;
		}

		int j;
		for (j = 0; j < p; j++) {
			int y_start = 0 + j * (DISP_SIZE_Y / p);
			int y_end = y_start + (DISP_SIZE_Y / p) - 1;

			uint32_t *buf = badge_eink_tmpbuf;
			badge_eink_create_bitplane(img, buf, i << 1, DISPLAY_FLAG_GREYSCALE|(flags & DISPLAY_FLAG_ROTATE_180));

			badge_eink_write_bitplane(buf, y_start, y_end);

			// LUT:
			//   Ignore old state;
			//   Do nothing when bit is not set;
			//   Make pixel whiter when bit is set;
			//   Duration is <ii> cycles.
			struct badge_eink_lut_entry lut[] = {
				{ .length = ii, .voltages = 0x88, },
				{ .length = 0 }
			};

			/* update display */
			struct badge_eink_update eink_upd = {
				.lut        = BADGE_EINK_LUT_CUSTOM,
				.lut_custom = lut,
				.reg_0x3a   = 0, // no dummy lines per gate
				.reg_0x3b   = 0, // 30us per line
				.y_start    = y_start,
				.y_end      = y_end + 1,
			};
			badge_eink_update(&eink_upd);
		}
	}
	badge_eink_have_oldbuf = false;
}

void
badge_eink_set_ram_area(uint8_t x_start, uint8_t x_end,
		uint16_t y_start, uint16_t y_end)
{
	// set RAM X - address Start / End position
	badge_eink_dev_write_command_p2(0x44, x_start, x_end);
	// set RAM Y - address Start / End position
	badge_eink_dev_write_command_p4(0x45, y_start & 0xff, y_start >> 8, y_end & 0xff, y_end >> 8);
}

void
badge_eink_set_ram_pointer(uint8_t x_addr, uint16_t y_addr)
{
	// set RAM X address counter
	badge_eink_dev_write_command_p1(0x4e, x_addr);
	// set RAM Y address counter
	badge_eink_dev_write_command_p2(0x4f, y_addr & 0xff, y_addr >> 8);
}

void
badge_eink_deep_sleep(void)
{
	// enter deep sleep
	badge_eink_dev_write_command_p1(0x10, 0x01);
}

void
badge_eink_wakeup(void)
{
	// leave deep sleep
	badge_eink_dev_write_command_p1(0x10, 0x00);
}

esp_err_t
badge_eink_init(void)
{
	static bool badge_eink_init_done = false;

	if (badge_eink_init_done)
		return ESP_OK;

	ESP_LOGD(TAG, "init called");

	// allocate buffers
	badge_eink_tmpbuf = heap_caps_malloc(DISP_SIZE_X_B * DISP_SIZE_Y, MALLOC_CAP_32BIT);
	if (badge_eink_tmpbuf == NULL)
		return ESP_ERR_NO_MEM;

#ifdef CONFIG_SHA_BADGE_EINK_DEPG0290B1
	badge_eink_oldbuf = heap_caps_malloc(DISP_SIZE_X_B * DISP_SIZE_Y, MALLOC_CAP_32BIT);
	if (badge_eink_oldbuf == NULL)
		return ESP_ERR_NO_MEM;
#endif // CONFIG_SHA_BADGE_EINK_DEPG0290B1

	// initialize spi interface to display
	esp_err_t res = badge_eink_dev_init();
	if (res != ESP_OK)
		return res;

#ifdef CONFIG_SHA_BADGE_EINK_GDEH029A1
	/* initialize GDEH029A1 */

	// Hardware reset
	badge_eink_dev_reset();

	// Software reset
	badge_eink_dev_write_command(0x12);

	// 0C: booster soft start control
	badge_eink_dev_write_command_p3(0x0c, 0xd7, 0xd6, 0x9d);

	// 2C: write VCOM register
	badge_eink_dev_write_command_p1(0x2c, 0xa8); // VCOM 7c

	// 11: data entry mode setting
	badge_eink_dev_write_command_p1(0x11, 0x03); // X inc, Y inc
#endif // CONFIG_SHA_BADGE_EINK_GDEH029A1

#ifdef CONFIG_SHA_BADGE_EINK_DEPG0290B1
	/* initialize DEPG0290B01 */

	// Hardware reset
	badge_eink_dev_reset();

	// Software reset
	badge_eink_dev_write_command(0x12);

	// Set analog block control
	badge_eink_dev_write_command_p1(0x74, 0x54);

	// Set digital block control
	badge_eink_dev_write_command_p1(0x7E, 0x3B);

	// Set display size and driver output control
	badge_eink_dev_write_command_p3(0x01, 0x27, 0x01, 0x00);

	// Ram data entry mode
	// Adress counter is updated in Y direction, Y increment, X increment
	badge_eink_dev_write_command_p1(0x11, 0x03);

	// Set RAM X address (00h to 0Fh)
	badge_eink_dev_write_command_p2(0x44, 0x00, 0x0F);

	// Set RAM Y address (0127h to 0000h)
	badge_eink_dev_write_command_p4(0x45, 0x00, 0x00, 0x27, 0x01);

	// Set border waveform for VBD (see datasheet)
	badge_eink_dev_write_command_p1(0x3C, 0x01);

	// SET VOLTAGE

	// Set VCOM value
	badge_eink_dev_write_command_p1(0x2C, 0x26);

	// Gate voltage setting (17h = 20 Volt, ranges from 10v to 21v)
	badge_eink_dev_write_command_p1(0x03, 0x17);

	// Source voltage setting (15volt, 0 volt and -15 volt)
	badge_eink_dev_write_command_p3(0x04, 0x41, 0x00, 0x32);
#endif // CONFIG_SHA_BADGE_EINK_DEPG0290B1

	badge_eink_init_done = true;

	ESP_LOGD(TAG, "init done");

	return ESP_OK;
}
