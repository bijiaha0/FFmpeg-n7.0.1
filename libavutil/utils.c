/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"
#include "avutil.h"
#include "avassert.h"

/**
 * @file
 * various utility functions
 */

const char *av_get_media_type_string(enum AVMediaType media_type)
{
    switch (media_type) {
    case AVMEDIA_TYPE_VIDEO:      return "video";
    case AVMEDIA_TYPE_AUDIO:      return "audio";
    case AVMEDIA_TYPE_DATA:       return "data";
    case AVMEDIA_TYPE_SUBTITLE:   return "subtitle";
    case AVMEDIA_TYPE_ATTACHMENT: return "attachment";
    default:                      return NULL;
    }
}

char av_get_picture_type_char(enum AVPictureType pict_type)
{
    switch (pict_type) {
    case AV_PICTURE_TYPE_I:  return 'I';
    case AV_PICTURE_TYPE_P:  return 'P';
    case AV_PICTURE_TYPE_B:  return 'B';
    case AV_PICTURE_TYPE_S:  return 'S';
    case AV_PICTURE_TYPE_SI: return 'i';
    case AV_PICTURE_TYPE_SP: return 'p';
    case AV_PICTURE_TYPE_BI: return 'b';
    default:                 return '?';
    }
}

unsigned av_int_list_length_for_size(unsigned elsize,
                                     const void *list, uint64_t term)
{
    unsigned i;

    if (!list)
        return 0;
#define LIST_LENGTH(type) \
    { type t = term, *l = (type *)list; for (i = 0; l[i] != t; i++); }
    switch (elsize) {
    case 1: LIST_LENGTH(uint8_t);  break;
    case 2: LIST_LENGTH(uint16_t); break;
    case 4: LIST_LENGTH(uint32_t); break;
    case 8: LIST_LENGTH(uint64_t); break;
    default: av_assert0(!"valid element size");
    }
    return i;
}

char *av_fourcc_make_string(char *buf, uint32_t fourcc)
{
    int i;
    char *orig_buf = buf;
    size_t buf_size = AV_FOURCC_MAX_STRING_SIZE;

    for (i = 0; i < 4; i++) {
        const int c = fourcc & 0xff;
        const int print_chr = (c >= '0' && c <= '9') ||
                              (c >= 'a' && c <= 'z') ||
                              (c >= 'A' && c <= 'Z') ||
                              (c && strchr(". -_", c));
        const int len = snprintf(buf, buf_size, print_chr ? "%c" : "[%d]", c);
        if (len < 0)
            break;
        buf += len;
        buf_size = buf_size > len ? buf_size - len : 0;
        fourcc >>= 8;
    }

    return orig_buf;
}

AVRational av_get_time_base_q(void)
{
    return (AVRational){1, AV_TIME_BASE};
}

hb_dovi_conf_t ff_dovi;

hb_mastering_display_metadata_t ff_mastering;

hb_dovi_conf_t hb_dovi_ff_to_hb(AVDOVIDecoderConfigurationRecord dovi)
{
    hb_dovi_conf_t hb_dovi;

    hb_dovi.dv_version_major = dovi.dv_version_major;
    hb_dovi.dv_version_minor = dovi.dv_version_minor;
    hb_dovi.dv_profile = dovi.dv_profile;
    hb_dovi.dv_level = dovi.dv_level;
    hb_dovi.rpu_present_flag = dovi.rpu_present_flag;
    hb_dovi.el_present_flag = dovi.el_present_flag;
    hb_dovi.bl_present_flag = dovi.bl_present_flag;
    hb_dovi.dv_bl_signal_compatibility_id = dovi.dv_bl_signal_compatibility_id;

    return hb_dovi;
}

AVDOVIDecoderConfigurationRecord hb_dovi_hb_to_ff(hb_dovi_conf_t dovi)
{
    AVDOVIDecoderConfigurationRecord res_dovi;

    res_dovi.dv_version_major = dovi.dv_version_major;
    res_dovi.dv_version_minor = dovi.dv_version_minor;
    res_dovi.dv_profile = dovi.dv_profile;
    res_dovi.dv_level = dovi.dv_level;
    res_dovi.rpu_present_flag = dovi.rpu_present_flag;
    res_dovi.el_present_flag = dovi.el_present_flag;
    res_dovi.bl_present_flag = dovi.bl_present_flag;
    res_dovi.dv_bl_signal_compatibility_id = dovi.dv_bl_signal_compatibility_id;

    return res_dovi;
}

static struct
{
    const uint32_t id;
    const uint32_t max_pps;
    const uint32_t max_width;
    const uint32_t max_bitrate_main_tier;
    const uint32_t max_bitrate_high_tier;
}
        hb_dovi_levels[] =
        {
                { 1,  22118400,   1280, 20,  50  },
                { 2,  27648000,   1280, 20,  50  },
                { 3,  49766400,   1920, 20,  70  },
                { 4,  62208000,   2560, 20,  70  },
                { 5,  124416000,  3840, 20,  70  },
                { 6,  199065600,  3840, 25,  130 },
                { 7,  248832000,  3840, 25,  130 },
                { 8,  398131200,  3840, 40,  130 },
                { 9,  497664000,  3840, 40,  130 },
                { 10, 995328000,  3840, 60,  240 },
                { 11, 995328000,  7680, 60,  240 },
                { 12, 1990656000, 7680, 120, 480 },
                { 13, 3981312000, 7680, 240, 800 },
                { 0, 0, 0, 0, 0 }
        };

static struct
{
    const char *level;
    const int level_id;
    const uint32_t max_luma_sample_rate;
    const uint32_t max_luma_picture_size;
    const uint32_t max_bitrate_main_tier;
    const uint32_t max_bitrate_high_tier;
}

        hb_h265_level_limits[] =
        {
                { "1.0", 10, 552960,     36864,    128,    128    },
                { "2.0", 20, 3686400,    122880,   1500,   1500   },
                { "2.1", 31, 7372800,    245760,   3000,   3000   },
                { "3.0", 30, 16588800,   552960,   6000,   6000   },
                { "3.1", 31, 33177600,   983040,   10000,  10000  },
                { "4.0", 40, 66846720,   2228224,  12000,  30000  },
                { "4.1", 41, 133693440,  2228224,  20000,  50000  },
                { "5.0", 50, 267386880,  8912896,  25000,  100000 },
                { "5.1", 51, 534773760,  8912896,  40000,  160000 },
                { "5.2", 52, 1069547520, 8912896,  60000,  240000 },
                { "6.0", 60, 1069547520, 35651584, 60000,  240000 },
                { "6.1", 61, 2139095040, 35651584, 120000, 480000 },
                { "6.2", 62, 4278190080, 35651584, 240000, 800000 },
                { NULL,  0,  0,          0,        0,      0      }
        };

// From AV1 Annex A
static struct
{
    const char *level;
    const int level_id;
    const uint32_t max_pic_size;
    const uint32_t max_h_size;
    const uint32_t max_v_size;
    const uint32_t max_decode_rate;
    const uint32_t max_bitrate_main_tier;
    const uint32_t max_bitrate_high_tier;
}
        hb_av1_level_limits[] =
        {
                { "2.0", 20,   147456,  2048, 1152,    4423680,   1500,   1500 },
                { "2.1", 31,   278784,  2816, 1584,    8363520,   3000,   3000 },
                { "2.2", 31,   278784,  2816, 3000,    8363520,   3000,   3000 },
                { "2.3", 31,   278784,  2816, 3000,    8363520,   3000,   3000 },
                { "3.0", 30,   665856,  4352, 2448,   19975680,   6000,   6000 },
                { "3.1", 31,   665856,  5504, 3096,   31950720,  10000,  10000 },
                { "3.2", 31,   665856,  5504, 3096,   31950720,  10000,  10000 },
                { "3.3", 31,   665856,  5504, 3096,   31950720,  10000,  10000 },
                { "4.0", 40,  2359296,  6144, 3456,   70778880,  12000,  30000 },
                { "4.1", 40,  2359296,  6144, 3456,  141557760,  20000,  50000 },
                { "4.2", 40,  2359296,  6144, 3456,  141557760,  20000,  50000 },
                { "4.3", 40,  2359296,  6144, 3456,  141557760,  20000,  50000 },
                { "5.0", 50,  8912896,  8192, 4352,  267386880,  30000, 100000 },
                { "5.1", 51,  8912896,  8192, 4352,  534773760,  40000, 160000 },
                { "5.2", 52,  8912896,  8192, 4352, 1069547520,  60000, 240000 },
                { "5.3", 52,  8912896,  8192, 4352, 1069547520,  60000, 240000 },
                { "6.0", 60, 35651584, 16384, 8704, 1069547520,  60000, 240000 },
                { "6.1", 61, 35651584, 16384, 8704, 2139095040, 100000, 480000 },
                { "6.2", 62, 35651584, 16384, 8704, 4278190080, 160000, 800000 },
                { "6.3", 62, 35651584, 16384, 8704, 4278190080, 160000, 800000 },
                { "7.0", 62, 35651584, 16384, 8704, 4278190080, 160000, 800000 },
                { "7.1", 62, 35651584, 16384, 8704, 4278190080, 160000, 800000 },
                { "7.2", 62, 35651584, 16384, 8704, 4278190080, 160000, 800000 },
                { "7.3", 62, 35651584, 16384, 8704, 4278190080, 160000, 800000 },
                {  NULL,  0,        0,     0,   0,           0,      0,      0 }
        };

int hb_dovi_max_rate(int vcodec, int width, int pps, int bitrate, int level, int high_tier)
{
    int max_rate = 0;
    if (level)
    {
        if (vcodec & HB_VCODEC_H265_MASK)
        {
            for (int i = 0; hb_h265_level_limits[i].level_id != 0; i++)
            {
                if (hb_h265_level_limits[i].level_id == level)
                {
                    max_rate = high_tier ?
                               hb_h265_level_limits[i].max_bitrate_high_tier :
                               hb_h265_level_limits[i].max_bitrate_main_tier;
                    break;
                }
            }
        }
        else if (vcodec & HB_VCODEC_AV1_MASK)
        {
            for (int i = 0; hb_av1_level_limits[i].level_id != 0; i++)
            {
                if (i == level)
                {
                    max_rate = high_tier ?
                               hb_av1_level_limits[i].max_bitrate_high_tier :
                               hb_av1_level_limits[i].max_bitrate_main_tier;
                    break;
                }
            }
        }
    }
    else
    {
        for (int i = 0; hb_dovi_levels[i].id != 0; i++)
        {
            int level_max_rate = high_tier ?
                                 hb_dovi_levels[i].max_bitrate_high_tier :
                                 hb_dovi_levels[i].max_bitrate_main_tier;

            if (pps <= hb_dovi_levels[i].max_pps &&
                width <= hb_dovi_levels[i].max_width &&
                bitrate <= level_max_rate * 1000)
            {
                max_rate = level_max_rate * 1000;
                break;
            }
        }
    }

    return max_rate;
}

int hb_dovi_level(int width, int pps, int max_rate, int high_tier)
{
    int dv_level = hb_dovi_levels[12].id;
    ;

    for (int i = 0; hb_dovi_levels[i].id != 0; i++)
    {
        int max_pps = hb_dovi_levels[i].max_pps;
        int max_width = hb_dovi_levels[i].max_width;
        int tier_max_rate = high_tier ?
                            hb_dovi_levels[i].max_bitrate_high_tier :
                            hb_dovi_levels[i].max_bitrate_main_tier;

        tier_max_rate *= 1000;

        if (pps <= max_pps && max_rate <= tier_max_rate && width <= max_width)
        {
            dv_level = hb_dovi_levels[i].id;
            break;
        }
    }

    return dv_level;
}

void hb_bitstream_init(hb_bitstream_t *bs,
                       uint8_t *buf,
                       uint32_t buf_size,
                       int clear)
{
    bs->pos = 0;
    bs->buf = buf;
    bs->buf_size = buf_size << 3;
    if (clear)
    {
        memset(bs->buf, 0, buf_size);
    }
}

void hb_bitstream_put_bytes(hb_bitstream_t *bs,
                            uint8_t *bytes,
                            uint32_t num_bytes)
{
    uint32_t num_bits = num_bytes << 3;

    if (num_bits + bs->pos > bs->buf_size)
    {
        return;
    }

    if ((bs->pos & 7) == 0)
    {
        memcpy(&bs->buf[bs->pos >> 3], bytes, num_bytes);
        bs->pos += num_bits;
    }
    else
    {
        for (uint32_t i = 0; i < num_bytes; i++)
        {
            hb_bitstream_put_bits(bs, bytes[i], 8);
        }
    }
}

void hb_bitstream_put_bits(hb_bitstream_t *bs,
                           uint32_t bits,
                           uint32_t num_bits)
{
    if (num_bits + bs->pos > bs->buf_size)
    {
        return;
    }
    if (num_bits > 32) {
        return;
    }

    for (int8_t i = num_bits - 1; i >= 0; i--)
    {
        bs->buf[bs->pos >> 3] |= ((bits >> i) & 1) << (7 - (bs->pos & 7));
        bs->pos++;
    }

}

uint32_t hb_bitstream_peak_bits(hb_bitstream_t *bs,
                                uint32_t num_bits)
{
    if (num_bits + bs->pos > bs->buf_size)
    {
        return 0;
    }
    if (num_bits > 32) {
        return 0;
    }

    uint32_t value = 0;
    uint32_t pos = bs->pos;

    for (uint8_t i = 0; i < num_bits; i++)
    {
        value <<= 1;
        value |= (bs->buf[pos >> 3] >> (7 - (pos & 7))) & 1;
        pos++;
    }

    return value;
}

uint32_t hb_bitstream_get_bits(hb_bitstream_t *bs,
                               uint32_t num_bits)
{
    if (num_bits + bs->pos > bs->buf_size)
    {
        return 0;
    }
    if (num_bits > 32)
    {
        return 0;
    }

    uint32_t value = 0;

    for (uint8_t i = 0; i < num_bits; i++)
    {
        value <<= 1;
        value |= (bs->buf[bs->pos >> 3] >> (7 - (bs->pos & 7))) & 1;
        bs->pos++;
    }

    return value;
}

void hb_bitstream_skip_bytes(hb_bitstream_t *bs,
                             uint32_t num_bytes)
{
    hb_bitstream_skip_bits(bs, num_bytes << 3);
}

void hb_bitstream_skip_bits(hb_bitstream_t *bs,
                            uint32_t num_bits)
{
    hb_bitstream_set_bit_position(bs, hb_bitstream_get_bit_position(bs) + num_bits);
}

uint32_t hb_bitstream_get_bit_position(hb_bitstream_t *bs)
{
    return bs->pos;
}

void hb_bitstream_set_bit_position(hb_bitstream_t *bs,
                                   uint32_t pos)
{
    if (pos > bs->buf_size)
    {
        return;
    }
    bs->pos = pos;
}

uint8_t * hb_bitstream_get_buffer(hb_bitstream_t *bs)
{
    return bs->buf;
}

uint32_t hb_bitstream_get_count_of_bytes(hb_bitstream_t *bs)
{
    return (hb_bitstream_get_count_of_bits(bs) + 7) / 8;
}

uint32_t hb_bitstream_get_count_of_bits(hb_bitstream_t *bs)
{
    return bs->buf_size;
}

uint32_t hb_bitstream_get_count_of_used_bytes(hb_bitstream_t *bs)
{
    return (bs->pos + 7) / 8;
}

uint32_t hb_bitstream_get_remaining_bits(hb_bitstream_t *bs)
{
    return bs->buf_size - bs->pos;
}

void hb_dynamic_hdr10_plus_to_itu_t_t35(const AVDynamicHDRPlus *s, uint8_t **buf_p, uint32_t *size)
{
    const uint8_t countryCode = 0xB5;
    const uint16_t terminalProviderCode = 0x003C;
    const uint16_t terminalProviderOrientedCode = 0x0001;
    const uint8_t applicationIdentifier = 4;

    uint8_t *buf = av_mallocz(2048);
    hb_bitstream_t bs;

    hb_bitstream_init(&bs, buf, 2048, 0);

    hb_bitstream_put_bits(&bs, countryCode, 8);
    hb_bitstream_put_bits(&bs, terminalProviderCode, 16);
    hb_bitstream_put_bits(&bs, terminalProviderOrientedCode, 16);

    hb_bitstream_put_bits(&bs, applicationIdentifier, 8);
    hb_bitstream_put_bits(&bs, s->application_version, 8);
    hb_bitstream_put_bits(&bs, s->num_windows, 2);

    for (int w = 1; w < s->num_windows; w++)
    {
        const AVHDRPlusColorTransformParams *params = &s->params[w];

        hb_bitstream_put_bits(&bs, params->window_upper_left_corner_x.num, 16);
        hb_bitstream_put_bits(&bs, params->window_upper_left_corner_x.num, 16);
        hb_bitstream_put_bits(&bs, params->window_upper_left_corner_x.num, 16);
        hb_bitstream_put_bits(&bs, params->window_upper_left_corner_x.num, 16);

        hb_bitstream_put_bits(&bs, params->center_of_ellipse_x, 16);
        hb_bitstream_put_bits(&bs, params->center_of_ellipse_y, 16);
        hb_bitstream_put_bits(&bs, params->rotation_angle, 8);
        hb_bitstream_put_bits(&bs, params->semimajor_axis_internal_ellipse, 16);
        hb_bitstream_put_bits(&bs, params->semimajor_axis_external_ellipse, 16);
        hb_bitstream_put_bits(&bs, params->semiminor_axis_external_ellipse, 16);
        hb_bitstream_put_bits(&bs, params->overlap_process_option, 1);
    }

    hb_bitstream_put_bits(&bs, s->targeted_system_display_maximum_luminance.num, 27);
    hb_bitstream_put_bits(&bs, s->targeted_system_display_actual_peak_luminance_flag, 1);

    if (s->targeted_system_display_actual_peak_luminance_flag)
    {
        hb_bitstream_put_bits(&bs, s->num_rows_targeted_system_display_actual_peak_luminance, 5);
        hb_bitstream_put_bits(&bs, s->num_cols_targeted_system_display_actual_peak_luminance, 5);

        for (int i = 0; i < s->num_rows_targeted_system_display_actual_peak_luminance; i++)
        {
            for (int j = 0; j < s->num_cols_targeted_system_display_actual_peak_luminance; j++)
            {
                hb_bitstream_put_bits(&bs, s->targeted_system_display_actual_peak_luminance[i][j].num, 4);
            }
        }
    }

    for (int w = 0; w < s->num_windows; w++)
    {
        const AVHDRPlusColorTransformParams *params = &s->params[w];

        for (int i = 0; i < 3; i++)
        {
            hb_bitstream_put_bits(&bs, params->maxscl[i].num, 17);
        }
        hb_bitstream_put_bits(&bs, params->average_maxrgb.num, 17);
        hb_bitstream_put_bits(&bs, params->num_distribution_maxrgb_percentiles, 4);

        for (int i = 0; i < params->num_distribution_maxrgb_percentiles; i++)
        {
            hb_bitstream_put_bits(&bs, params->distribution_maxrgb[i].percentage, 7);
            hb_bitstream_put_bits(&bs, params->distribution_maxrgb[i].percentile.num, 17);
        }

        hb_bitstream_put_bits(&bs, params->fraction_bright_pixels.num, 10);
    }

    hb_bitstream_put_bits(&bs, s->mastering_display_actual_peak_luminance_flag, 1);

    if (s->mastering_display_actual_peak_luminance_flag)
    {
        hb_bitstream_put_bits(&bs, s->num_rows_mastering_display_actual_peak_luminance, 5);
        hb_bitstream_put_bits(&bs, s->num_cols_mastering_display_actual_peak_luminance, 5);

        for (int i = 0; i < s->num_rows_mastering_display_actual_peak_luminance; i++)
        {
            for (int j = 0; j < s->num_cols_mastering_display_actual_peak_luminance; j++)
            {
                hb_bitstream_put_bits(&bs, s->mastering_display_actual_peak_luminance[i][j].num, 4);
            }
        }
    }

    for (int w = 0; w < s->num_windows; w++)
    {
        const AVHDRPlusColorTransformParams *params = &s->params[w];

        hb_bitstream_put_bits(&bs, params->tone_mapping_flag, 1);
        if (params->tone_mapping_flag)
        {
            hb_bitstream_put_bits(&bs, params->knee_point_x.num, 12);
            hb_bitstream_put_bits(&bs, params->knee_point_y.num, 12);

            hb_bitstream_put_bits(&bs, params->num_bezier_curve_anchors, 4);

            for (int i = 0; i < params->num_bezier_curve_anchors; i++)
            {
                hb_bitstream_put_bits(&bs, params->bezier_curve_anchors[i].num, 10);
            }
        }

        hb_bitstream_put_bits(&bs, params->color_saturation_mapping_flag, 1);

        if (params->color_saturation_mapping_flag)
        {
            hb_bitstream_put_bits(&bs, params->color_saturation_weight.num, 6);
        }
    }

    *buf_p = buf;
    *size = hb_bitstream_get_count_of_used_bytes(&bs);
}


void av_assert0_fpu(void) {
#if HAVE_MMX_INLINE
    uint16_t state[14];
     __asm__ volatile (
        "fstenv %0 \n\t"
        : "+m" (state)
        :
        : "memory"
    );
    av_assert0((state[4] & 3) == 3);
#endif
}
