#include "libavutil/avassert.h"
#include "libavutil/frame.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "libavutil/mem.h"
#include <stdio.h>
#include <stdlib.h>
#include "libdovi/rpu_parser.h"

#define RPU_MODE_UPDATE_ACTIVE_AREA  1
#define RPU_MODE_CONVERT_TO_8_1      2

#define RPU_MODE_EMIT_UNSPECT_62_NAL 4
#define RPU_MODE_EMIT_T35_OBU        8

typedef struct RpuConverterContext {
    int        mode;
    AVBufferRef        *rpu;
} RpuConverterContext;

static void save_rpu(RpuConverterContext *context, AVBufferRef *ref)
{
    av_buffer_unref(&context->rpu);
    context->rpu = av_buffer_ref(ref);
}

static void apply_rpu_if_needed(RpuConverterContext *context, AVFrame *frame)
{
    int rpu_available = 0;
    enum AVFrameSideDataType type = AV_FRAME_DATA_DOVI_RPU_BUFFER;

    for (int i = 0; i < frame->nb_side_data; i++)
    {
        const AVFrameSideData *side_data = frame->side_data[i];
        if (side_data->type == AV_FRAME_DATA_DOVI_RPU_BUFFER)
        {
            type = side_data->type;
            rpu_available = 1;
        }
    }

    if (rpu_available == 0)
    {
        if (context->rpu)
        {
            AVBufferRef *ref = av_buffer_ref(context->rpu);
            AVFrameSideData *sd_dst = av_frame_new_side_data_from_buf(frame, type, ref);
            if (!sd_dst)
            {
                av_buffer_unref(&ref);
            }
            av_log(NULL, AV_LOG_INFO,"rpu: missing rpu, falling back to last seen rpu");
        }
        else
        {
            av_log(NULL, AV_LOG_INFO,"rpu: missing rpu, no fallback available");
        }
    }
}


static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    RpuConverterContext *context = ctx->priv;

    AVFilterLink *outlink = ctx->outputs[0];

    // libavcodec hevc decoder seems to be missing some rpu
    // in a specific sample file, work around the issue
    // by using the last seen rpu
    apply_rpu_if_needed(context, frame);

    for (int i = 0; i < frame->nb_side_data; i++)
    {
        const AVFrameSideData *side_data = frame->side_data[i];
        if (side_data->type == AV_FRAME_DATA_DOVI_RPU_BUFFER)
        {
            DoviRpuOpaque *rpu_in = NULL;
            if (side_data->type == AV_FRAME_DATA_DOVI_RPU_BUFFER)
            {
                rpu_in = dovi_parse_unspec62_nalu(side_data->data, side_data->size);
            }

            if (rpu_in == NULL)
            {
                av_log(ctx, AV_LOG_INFO,"rpu: dovi_parse failed");
                break;
            }

            if (context->mode & RPU_MODE_CONVERT_TO_8_1)
            {
                const DoviRpuDataHeader *header = dovi_rpu_get_header(rpu_in);
                if (header && header->guessed_profile == 7)
                {
                    // Convert the BL to 8.1
                    int ret = dovi_convert_rpu_with_mode(rpu_in, 2);
                    if (ret < 0)
                    {
                        av_log(ctx, AV_LOG_INFO,"rpu: dovi_convert_rpu_with_mode failed");
                    }
                }

                if (header)
                {
                    dovi_rpu_free_header(header);
                }
            }

            save_rpu(context, side_data->buf);

            if (context->mode)
            {
                const DoviData *rpu_data = NULL;

                if (context->mode & RPU_MODE_EMIT_UNSPECT_62_NAL)
                {
                    rpu_data = dovi_write_unspec62_nalu(rpu_in);
                }

                if (rpu_data)
                {
                    av_frame_remove_side_data(frame, side_data->type);
                    const int offset = context->mode & RPU_MODE_EMIT_UNSPECT_62_NAL ? 2 : 0;

                    AVBufferRef *ref = av_buffer_alloc(rpu_data->len - offset);
                    memcpy(ref->data, rpu_data->data + offset, rpu_data->len - offset);
                    AVFrameSideData *sd_dst = NULL;

                    if (context->mode & RPU_MODE_EMIT_UNSPECT_62_NAL)
                    {
                        sd_dst = av_frame_new_side_data_from_buf(frame, AV_FRAME_DATA_DOVI_RPU_BUFFER, ref);
                    }

                    if (!sd_dst)
                    {
                        av_buffer_unref(&ref);
                    }

                    dovi_data_free(rpu_data);
                }
                else
                {
                    av_log(ctx, AV_LOG_INFO,"rpu: dovi_write failed");
                }
            }

            dovi_rpu_free(rpu_in);

            break;
        }
    }

    return ff_filter_frame(outlink, frame);

}


static av_cold int rpu_converter_init(AVFilterContext *ctx) {
    RpuConverterContext *context = ctx->priv;
    int mode = RPU_MODE_EMIT_UNSPECT_62_NAL;
    context->mode = mode;

    printf("rpu converter init.\n");
    return 0;
}

static av_cold void rpu_converter_uninit(AVFilterContext *ctx) {
    RpuConverterContext *context = ctx->priv;
    av_buffer_unref(&context->rpu);
    printf("rpu converter uninit.\n");
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_rpu_converter = {
    .name        = "rpu_converter",
    .description = NULL_IF_CONFIG_SMALL("rpu_converter."),
    .priv_size       = sizeof(RpuConverterContext),
    .init        = rpu_converter_init,
    .uninit      = rpu_converter_uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
};
