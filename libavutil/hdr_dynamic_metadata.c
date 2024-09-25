/**
 * Copyright (c) 2018 Mohammad Izadi <moh.izadi at gmail.com>
 *
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

#include "avassert.h"
#include "hdr_dynamic_metadata.h"
#include "mem.h"
#include "libavcodec/defs.h"
#include "libavcodec/get_bits.h"
#include "libavcodec/put_bits.h"
#include "libavcodec/itut35.h"

static const int64_t luminance_den = 1;
static const int32_t peak_luminance_den = 15;
static const int64_t rgb_den = 100000;
static const int32_t fraction_pixel_den = 1000;
static const int32_t knee_point_den = 4095;
static const int32_t bezier_anchor_den = 1023;
static const int32_t saturation_weight_den = 8;

AVDynamicHDRPlus *av_dynamic_hdr_plus_alloc(size_t *size)
{
    AVDynamicHDRPlus *hdr_plus = av_mallocz(sizeof(AVDynamicHDRPlus));
    if (!hdr_plus)
        return NULL;

    if (size)
        *size = sizeof(*hdr_plus);

    return hdr_plus;
}

AVDynamicHDRPlus *av_dynamic_hdr_plus_create_side_data(AVFrame *frame)
{
    AVFrameSideData *side_data = av_frame_new_side_data(frame,
                                                        AV_FRAME_DATA_DYNAMIC_HDR_PLUS,
                                                        sizeof(AVDynamicHDRPlus));
    if (!side_data)
        return NULL;

    memset(side_data->data, 0, sizeof(AVDynamicHDRPlus));

    return (AVDynamicHDRPlus *)side_data->data;
}

int av_dynamic_hdr_plus_from_t35(AVDynamicHDRPlus *s, const uint8_t *data,
                                 size_t size)
{
    uint8_t padded_buf[AV_HDR_PLUS_MAX_PAYLOAD_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    GetBitContext gbc, *gb = &gbc;
    int ret;

    if (!s)
        return AVERROR(ENOMEM);

    if (size > AV_HDR_PLUS_MAX_PAYLOAD_SIZE)
        return AVERROR(EINVAL);

    memcpy(padded_buf, data, size);
    // Zero-initialize the buffer padding to avoid overreads into uninitialized data.
    memset(padded_buf + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    ret = init_get_bits8(gb, padded_buf, size);
    if (ret < 0)
        return ret;

    if (get_bits_left(gb) < 10)
        return AVERROR_INVALIDDATA;

    s->application_version = get_bits(gb, 8);
    s->num_windows = get_bits(gb, 2);

    if (s->num_windows < 1 || s->num_windows > 3) {
        return AVERROR_INVALIDDATA;
    }

    if (get_bits_left(gb) < ((19 * 8 + 1) * (s->num_windows - 1)))
        return AVERROR_INVALIDDATA;

    for (int w = 1; w < s->num_windows; w++) {
        // The corners are set to absolute coordinates here. They should be
        // converted to the relative coordinates (in [0, 1]) in the decoder.
        AVHDRPlusColorTransformParams *params = &s->params[w];
        params->window_upper_left_corner_x =
            (AVRational){get_bits(gb, 16), 1};
        params->window_upper_left_corner_y =
            (AVRational){get_bits(gb, 16), 1};
        params->window_lower_right_corner_x =
            (AVRational){get_bits(gb, 16), 1};
        params->window_lower_right_corner_y =
            (AVRational){get_bits(gb, 16), 1};

        params->center_of_ellipse_x = get_bits(gb, 16);
        params->center_of_ellipse_y = get_bits(gb, 16);
        params->rotation_angle = get_bits(gb, 8);
        params->semimajor_axis_internal_ellipse = get_bits(gb, 16);
        params->semimajor_axis_external_ellipse = get_bits(gb, 16);
        params->semiminor_axis_external_ellipse = get_bits(gb, 16);
        params->overlap_process_option = get_bits1(gb);
    }

    if (get_bits_left(gb) < 28)
        return AVERROR_INVALIDDATA;

    s->targeted_system_display_maximum_luminance =
        (AVRational){get_bits_long(gb, 27), luminance_den};
    s->targeted_system_display_actual_peak_luminance_flag = get_bits1(gb);

    if (s->targeted_system_display_actual_peak_luminance_flag) {
        int rows, cols;
        if (get_bits_left(gb) < 10)
            return AVERROR_INVALIDDATA;
        rows = get_bits(gb, 5);
        cols = get_bits(gb, 5);
        if (((rows < 2) || (rows > 25)) || ((cols < 2) || (cols > 25))) {
            return AVERROR_INVALIDDATA;
        }
        s->num_rows_targeted_system_display_actual_peak_luminance = rows;
        s->num_cols_targeted_system_display_actual_peak_luminance = cols;

        if (get_bits_left(gb) < (rows * cols * 4))
            return AVERROR_INVALIDDATA;

        for (int i = 0; i < rows; i++) {
            for (int j = 0; j < cols; j++) {
                s->targeted_system_display_actual_peak_luminance[i][j] =
                    (AVRational){get_bits(gb, 4), peak_luminance_den};
            }
        }
    }
    for (int w = 0; w < s->num_windows; w++) {
        AVHDRPlusColorTransformParams *params = &s->params[w];
        if (get_bits_left(gb) < (3 * 17 + 17 + 4))
            return AVERROR_INVALIDDATA;

        for (int i = 0; i < 3; i++) {
            params->maxscl[i] =
                (AVRational){get_bits(gb, 17), rgb_den};
        }
        params->average_maxrgb =
            (AVRational){get_bits(gb, 17), rgb_den};
        params->num_distribution_maxrgb_percentiles = get_bits(gb, 4);

        if (get_bits_left(gb) <
            (params->num_distribution_maxrgb_percentiles * 24))
            return AVERROR_INVALIDDATA;

        for (int i = 0; i < params->num_distribution_maxrgb_percentiles; i++) {
            params->distribution_maxrgb[i].percentage = get_bits(gb, 7);
            params->distribution_maxrgb[i].percentile =
                (AVRational){get_bits(gb, 17), rgb_den};
        }

        if (get_bits_left(gb) < 10)
            return AVERROR_INVALIDDATA;

        params->fraction_bright_pixels = (AVRational){get_bits(gb, 10), fraction_pixel_den};
    }
    if (get_bits_left(gb) < 1)
        return AVERROR_INVALIDDATA;
    s->mastering_display_actual_peak_luminance_flag = get_bits1(gb);
    if (s->mastering_display_actual_peak_luminance_flag) {
        int rows, cols;
        if (get_bits_left(gb) < 10)
            return AVERROR_INVALIDDATA;
        rows = get_bits(gb, 5);
        cols = get_bits(gb, 5);
        if (((rows < 2) || (rows > 25)) || ((cols < 2) || (cols > 25))) {
            return AVERROR_INVALIDDATA;
        }
        s->num_rows_mastering_display_actual_peak_luminance = rows;
        s->num_cols_mastering_display_actual_peak_luminance = cols;

        if (get_bits_left(gb) < (rows * cols * 4))
            return AVERROR_INVALIDDATA;

        for (int i = 0; i < rows; i++) {
            for (int j = 0; j < cols; j++) {
                s->mastering_display_actual_peak_luminance[i][j] =
                    (AVRational){get_bits(gb, 4), peak_luminance_den};
            }
        }
    }

    for (int w = 0; w < s->num_windows; w++) {
        AVHDRPlusColorTransformParams *params = &s->params[w];
        if (get_bits_left(gb) < 1)
            return AVERROR_INVALIDDATA;

        params->tone_mapping_flag = get_bits1(gb);
        if (params->tone_mapping_flag) {
            if (get_bits_left(gb) < 28)
                return AVERROR_INVALIDDATA;

            params->knee_point_x =
                (AVRational){get_bits(gb, 12), knee_point_den};
            params->knee_point_y =
                (AVRational){get_bits(gb, 12), knee_point_den};
            params->num_bezier_curve_anchors = get_bits(gb, 4);

            if (get_bits_left(gb) < (params->num_bezier_curve_anchors * 10))
                return AVERROR_INVALIDDATA;

            for (int i = 0; i < params->num_bezier_curve_anchors; i++) {
                params->bezier_curve_anchors[i] =
                    (AVRational){get_bits(gb, 10), bezier_anchor_den};
            }
        }

        if (get_bits_left(gb) < 1)
            return AVERROR_INVALIDDATA;
        params->color_saturation_mapping_flag = get_bits1(gb);
        if (params->color_saturation_mapping_flag) {
            if (get_bits_left(gb) < 6)
                return AVERROR_INVALIDDATA;
            params->color_saturation_weight =
                (AVRational){get_bits(gb, 6), saturation_weight_den};
        }
    }

    return 0;
}

int av_dynamic_hdr_plus_to_t35(const AVDynamicHDRPlus *s, uint8_t **data, size_t *size)
{
    uint8_t *buf = av_mallocz(2048);
    size_t size_bits, size_bytes;
    PutBitContext pbc, *pb = &pbc;
    init_put_bits(pb, buf, 2048 << 3);

    /**
     * Buffer size per CTA-861-H p.253-254:
     * 48 header bits (excluded from the serialized payload)
     * 8 bits for application_mode
     * 2 bits for num_windows
     * 153 bits for window geometry, for each window above 1
     * 27 bits for targeted_system_display_maximum_luminance
     * 1-2511 bits for targeted system display peak luminance information
     * 82-442 bits per window for pixel distribution information
     * 1-2511 bits for mastering display peak luminance information
     * 1-179 bits per window for tonemapping information
     * 1-7 bits per window for color saturation mapping information
     * Total: 123-7249 bits, excluding trimmed header bits
     */
    size_bits = 48;
    size_bits += 8;

    size_bits += 2;

    for (int w = 1; w < s->num_windows; w++)
        size_bits += 153;

    size_bits += 27;

    size_bits += 1;
    if (s->targeted_system_display_actual_peak_luminance_flag)
        size_bits += 10 +
                     s->num_rows_targeted_system_display_actual_peak_luminance *
                     s->num_cols_targeted_system_display_actual_peak_luminance * 4;

    for (int w = 0; w < s->num_windows; w++)
        size_bits += 72 + s->params[w].num_distribution_maxrgb_percentiles * 24 + 10;

    size_bits += 1;
    if (s->mastering_display_actual_peak_luminance_flag)
        size_bits += 10 +
                     s->num_rows_mastering_display_actual_peak_luminance *
                     s->num_cols_mastering_display_actual_peak_luminance * 4;

    for (int w = 0; w < s->num_windows; w++) {
        size_bits += 1;
        if (s->params[w].tone_mapping_flag)
            size_bits += 28 + s->params[w].num_bezier_curve_anchors * 10;

        size_bits += 1;
        if (s->params[w].color_saturation_mapping_flag)
            size_bits += 6;
    }

    size_bytes = (size_bits + 7) / 8;

    av_log(NULL, AV_LOG_INFO, "size_bits: %d, size_bytes: %d\n", size_bits, size_bytes);

    av_assert0(size_bytes <= 2048);

    const uint8_t countryCode = 0xB5;
    const uint16_t terminalProviderCode = 0x003C;
    const uint16_t terminalProviderOrientedCode = 0x0001;
    const uint8_t applicationIdentifier = 4;

    put_bits(pb, 8, ITU_T_T35_COUNTRY_CODE_US);
    put_bits(pb, 16, ITU_T_T35_PROVIDER_CODE_SMTPE);
    put_bits(pb, 16, 0x01);
    put_bits(pb, 8, 0x04);

    // 10
    // application_mode is set to Application Version 1
    put_bits(pb, 8, s->application_version);

    // Payload as per CTA-861-H p.253-254
    put_bits(pb, 2, s->num_windows);

    for (int w = 1; w < s->num_windows; w++) {
        // 64
        put_bits(pb, 16, s->params[w].window_upper_left_corner_x.num);
        put_bits(pb, 16, s->params[w].window_upper_left_corner_y.num);
        put_bits(pb, 16, s->params[w].window_lower_right_corner_x.num);
        put_bits(pb, 16, s->params[w].window_lower_right_corner_y.num);

        // 89
        put_bits(pb, 16, s->params[w].center_of_ellipse_x);
        put_bits(pb, 16, s->params[w].center_of_ellipse_y);
        put_bits(pb, 8, s->params[w].rotation_angle);
        put_bits(pb, 16, s->params[w].semimajor_axis_internal_ellipse);
        put_bits(pb, 16, s->params[w].semimajor_axis_external_ellipse);
        put_bits(pb, 16, s->params[w].semiminor_axis_external_ellipse);
        put_bits(pb, 1, s->params[w].overlap_process_option);
    }

    //28
    put_bits(pb, 27, s->targeted_system_display_maximum_luminance.num);
    put_bits(pb, 1, s->targeted_system_display_actual_peak_luminance_flag);

    if (s->targeted_system_display_actual_peak_luminance_flag) {
        //10
        put_bits(pb, 5, s->num_rows_targeted_system_display_actual_peak_luminance);
        put_bits(pb, 5, s->num_cols_targeted_system_display_actual_peak_luminance);

        for (int i = 0; i < s->num_rows_targeted_system_display_actual_peak_luminance; i++) {
            for (int j = 0; j < s->num_cols_targeted_system_display_actual_peak_luminance; j++)
                put_bits(pb, 4, s->targeted_system_display_actual_peak_luminance[i][j].num * peak_luminance_den /
                    s->targeted_system_display_actual_peak_luminance[i][j].den);
        }
    }

    for (int w = 0; w < s->num_windows; w++) {
        for (int i = 0; i < 3; i++)
            put_bits(pb, 17, s->params[w].maxscl[i].num * rgb_den / s->params[w].maxscl[i].den);
        put_bits(pb, 17, s->params[w].average_maxrgb.num * rgb_den / s->params[w].average_maxrgb.den);
        put_bits(pb, 4, s->params[w].num_distribution_maxrgb_percentiles);
        for (int i = 0; i < s->params[w].num_distribution_maxrgb_percentiles; i++) {
            put_bits(pb, 7, s->params[w].distribution_maxrgb[i].percentage);
            put_bits(pb, 17, s->params[w].distribution_maxrgb[i].percentile.num * rgb_den /
                s->params[w].distribution_maxrgb[i].percentile.den);
        }
        put_bits(pb, 10, s->params[w].fraction_bright_pixels.num * fraction_pixel_den /
            s->params[w].fraction_bright_pixels.den);
    }

    put_bits(pb, 1, s->mastering_display_actual_peak_luminance_flag);
    if (s->mastering_display_actual_peak_luminance_flag) {
        put_bits(pb, 5, s->num_rows_mastering_display_actual_peak_luminance);
        put_bits(pb, 5, s->num_cols_mastering_display_actual_peak_luminance);
        for (int i = 0; i < s->num_rows_mastering_display_actual_peak_luminance; i++) {
            for (int j = 0; j < s->num_cols_mastering_display_actual_peak_luminance; j++)
                put_bits(pb, 4, s->mastering_display_actual_peak_luminance[i][j].num * peak_luminance_den /
                    s->mastering_display_actual_peak_luminance[i][j].den);
        }
    }

    for (int w = 0; w < s->num_windows; w++) {
        put_bits(pb, 1, s->params[w].tone_mapping_flag);
        if (s->params[w].tone_mapping_flag) {
            put_bits(pb, 12, s->params[w].knee_point_x.num * knee_point_den / s->params[w].knee_point_x.den);
            put_bits(pb, 12, s->params[w].knee_point_y.num * knee_point_den / s->params[w].knee_point_y.den);
            put_bits(pb, 4, s->params[w].num_bezier_curve_anchors);
            for (int i = 0; i < s->params[w].num_bezier_curve_anchors; i++)
                put_bits(pb, 10, s->params[w].bezier_curve_anchors[i].num * bezier_anchor_den /
                    s->params[w].bezier_curve_anchors[i].den);
            put_bits(pb, 1, s->params[w].color_saturation_mapping_flag);
            if (s->params[w].color_saturation_mapping_flag)
                put_bits(pb, 6, s->params[w].color_saturation_weight.num * saturation_weight_den /
                    s->params[w].color_saturation_weight.den);
        }
    }

    flush_put_bits(pb);

    av_log(NULL, AV_LOG_INFO, "bits: %d, bytes: %d\n", put_bits_count(pb), put_bytes_count(pb, 1));

    *data = buf;
    if (size)
        *size = size_bytes;
    return 0;
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
        hb_bitstream_put_bits(&bs, params->window_upper_left_corner_y.num, 16);
        hb_bitstream_put_bits(&bs, params->window_lower_right_corner_x.num, 16);
        hb_bitstream_put_bits(&bs, params->window_lower_right_corner_y.num, 16);

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

