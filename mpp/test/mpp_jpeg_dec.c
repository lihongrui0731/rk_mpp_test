#include "mpp_jpeg_dec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rk_mpi.h"
#include "mpp_buffer.h"
#include "mpp_frame.h"
#include "mpp_packet.h"

int mpp_decode_jpeg_stream(char *jpeg_data, size_t jpeg_size, char *yuv_data, size_t yuv_size)
{
    if (!jpeg_data || jpeg_size == 0 || !yuv_data || yuv_size == 0) {
        return -1;
    }

    MppCtx ctx = NULL;
    MppApi *mpi = NULL;
    MPP_RET ret = mpp_create(&ctx, &mpi);
    if (MPP_OK != ret) {
        printf("mpp_create failed\n");
        return -1;
    }

    ret = mpp_init(ctx, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG);
    if (MPP_OK != ret) {
        printf("mpp_init failed\n");
        mpp_destroy(ctx);
        return -1;
    }

    MppPacket packet = NULL;
    ret = mpp_packet_init(&packet, jpeg_data, jpeg_size);
    if (MPP_OK != ret) {
        printf("mpp_packet_init failed\n");
        mpp_destroy(ctx);
        return -1;
    }
    mpp_packet_set_pts(packet, 0);
    mpp_packet_set_eos(packet); // Set eos flag to indicate end of stream

    /* send packet */
    ret = mpi->decode_put_packet(ctx, packet);
    if (MPP_OK != ret) {
        printf("decode_put_packet failed\n");
        mpp_packet_deinit(&packet);
        mpp_destroy(ctx);
        return -1;
    }

    int get_frame = 0;
    int try_times = 50;

    while (try_times > 0 && !get_frame) {
        MppFrame frame = NULL;
        ret = mpi->decode_get_frame(ctx, &frame);

        if (MPP_ERR_TIMEOUT == ret) {
            usleep(2000);
            try_times--;
            continue;
        }

        if (MPP_OK != ret) {
            printf("decode_get_frame failed ret %d\n", ret);
            break;
        }

        if (frame) {
            if (mpp_frame_get_info_change(frame)) {
                /* The decoder buffer size and format changed, we need to acknowledge it */
                RK_U32 width = mpp_frame_get_width(frame);
                RK_U32 height = mpp_frame_get_height(frame);
                RK_U32 hor_stride = mpp_frame_get_hor_stride(frame);
                RK_U32 ver_stride = mpp_frame_get_ver_stride(frame);

                printf("info change: w:h [%d:%d] stride [%d:%d]\n", width, height, hor_stride, ver_stride);

                ret = mpi->control(ctx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);
                if (ret) {
                    printf("info change ready failed ret %d\n", ret);
                }

                mpp_frame_deinit(&frame);
                continue;
            }

            RK_U32 err_info = mpp_frame_get_errinfo(frame);
            RK_U32 discard = mpp_frame_get_discard(frame);
            RK_U32 is_eos = mpp_frame_get_eos(frame);

            if (err_info || discard) {
                printf("frame decode error %x discard %x\n", err_info, discard);
            } else {
                MppBuffer buffer = mpp_frame_get_buffer(frame);
                if (buffer) {
                    void *ptr = mpp_buffer_get_ptr(buffer);
                    RK_U32 frame_width = mpp_frame_get_width(frame);
                    RK_U32 frame_height = mpp_frame_get_height(frame);
                    RK_U32 hor_stride = mpp_frame_get_hor_stride(frame);
                    RK_U32 ver_stride = mpp_frame_get_ver_stride(frame);

                    // Actual width/height to copy
                    size_t w = frame_width;
                    size_t h = frame_height;

                    // Usually NV12 or YUV420SP. Copy Y plane
                    char *dst = yuv_data;
                    char *src = (char *)ptr;
                    size_t copied = 0;
                    int failed = 0;
                    size_t i;

                    // Copy Y
                    for (i = 0; i < h; i++) {
                        if (copied + w > yuv_size) { failed = 1; break; }
                        memcpy(dst, src, w);
                        dst += w;
                        src += hor_stride;
                        copied += w;
                    }

                    // Copy UV (assuming NV12/NV21, height/2 rows, same width)
                    src = ((char *)ptr) + hor_stride * ver_stride;
                    for (i = 0; i < h / 2; i++) {
                        if (copied + w > yuv_size) { failed = 1; break; }
                        memcpy(dst, src, w);
                        dst += w;
                        src += hor_stride;
                        copied += w;
                    }

                    if (!failed) {
                        get_frame = 1;
                    }
                }
            }

            mpp_frame_deinit(&frame);

            if (is_eos) {
                break;
            }
        } else {
            usleep(2000);
            try_times--;
        }
    }

    /* clean up */
    mpp_packet_deinit(&packet);

    // reset decoder and wait for clean up
    mpi->reset(ctx);
    mpp_destroy(ctx);

    return get_frame ? 0 : -1;
}
