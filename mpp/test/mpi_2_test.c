/*
 * 简化版 MPP JPEG 解码示例
 * 仅保留核心解码逻辑，依赖 rockchip mpp 库
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "rk_mpi.h"
#include "mpp_mem.h"
#include "mpp_time.h"
#include "mpp_common.h"

// 读取文件数据到内存
static RK_U8* read_file_to_mem(const char* file_path, size_t* file_size) {
    FILE* fp = fopen(file_path, "rb");
    if (!fp) {
        fprintf(stderr, "open file %s failed\n", file_path);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    *file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    RK_U8* data = (RK_U8*)mpp_malloc(*file_size);
    if (!data) {
        fprintf(stderr, "malloc file buffer failed\n");
        fclose(fp);
        return NULL;
    }

    fread(data, 1, *file_size, fp);
    fclose(fp);
    return data;
}

// 保存 YUV 数据到文件
static void save_yuv_to_file(MppFrame frame, const char* output_path) {
    FILE* fp = fopen(output_path, "wb");
    if (!fp) {
        fprintf(stderr, "open output file %s failed\n", output_path);
        return;
    }

    RK_U32 width = mpp_frame_get_width(frame);
    RK_U32 height = mpp_frame_get_height(frame);
    RK_U32 hor_stride = mpp_frame_get_hor_stride(frame);
    RK_U32 ver_stride = mpp_frame_get_ver_stride(frame);

    // Y 分量
    RK_U8* y_data = mpp_frame_get_buf_addr(frame, MPP_FRAME_BUFFER_Y);
    for (RK_U32 i = 0; i < height; i++) {
        fwrite(y_data + i * hor_stride, 1, width, fp);
    }

    // U 分量
    RK_U8* u_data = mpp_frame_get_buf_addr(frame, MPP_FRAME_BUFFER_U);
    for (RK_U32 i = 0; i < height / 2; i++) {
        fwrite(u_data + i * hor_stride / 2, 1, width / 2, fp);
    }

    // V 分量
    RK_U8* v_data = mpp_frame_get_buf_addr(frame, MPP_FRAME_BUFFER_V);
    for (RK_U32 i = 0; i < height / 2; i++) {
        fwrite(v_data + i * hor_stride / 2, 1, width / 2, fp);
    }

    fclose(fp);
    printf("save YUV to %s success (w:%d h:%d)\n", output_path, width, height);
}

// 核心 JPEG 解码函数
int mpp_jpeg_decode(const char* input_jpg, const char* output_yuv) {
    MPP_RET ret = MPP_OK;
    MppCtx ctx = NULL;
    MppApi* mpi = NULL;
    MppPacket packet = NULL;
    MppFrame frame = NULL;
    MppBufferGroup frm_grp = NULL;
    size_t file_size = 0;
    RK_U8* jpg_data = NULL;

    // 1. 读取 JPG 文件数据
    jpg_data = read_file_to_mem(input_jpg, &file_size);
    if (!jpg_data) {
        ret = -1;
        goto MPP_DECODE_OUT;
    }

    // 2. 创建 MPP 上下文
    ret = mpp_create(&ctx, &mpi);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_create failed ret:%d\n", ret);
        goto MPP_DECODE_OUT;
    }

    // 3. 初始化解码器 (JPEG 编码类型)
    ret = mpp_init(ctx, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_init failed ret:%d\n", ret);
        goto MPP_DECODE_OUT;
    }

    // 4. 初始化数据包并填充 JPG 数据
    ret = mpp_packet_init(&packet, jpg_data, file_size);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_packet_init failed ret:%d\n", ret);
        goto MPP_DECODE_OUT;
    }
    mpp_packet_set_eos(packet); // 设置 EOS 标识

    // 5. 发送解码数据包
    ret = mpi->decode_put_packet(ctx, packet);
    if (ret != MPP_OK) {
        fprintf(stderr, "decode_put_packet failed ret:%d\n", ret);
        goto MPP_DECODE_OUT;
    }

    // 6. 获取解码后的帧数据
    do {
        ret = mpi->decode_get_frame(ctx, &frame);
        if (ret == MPP_ERR_TIMEOUT) {
            usleep(1000); // 超时重试
            continue;
        }
        if (ret != MPP_OK) {
            fprintf(stderr, "decode_get_frame failed ret:%d\n", ret);
            goto MPP_DECODE_OUT;
        }

        if (frame) {
            // 处理帧信息变更（初始化输出缓冲区）
            if (mpp_frame_get_info_change(frame)) {
                RK_U32 width = mpp_frame_get_width(frame);
                RK_U32 height = mpp_frame_get_height(frame);
                RK_U32 buf_size = mpp_frame_get_buf_size(frame);

                // 创建输出缓冲区组
                if (!frm_grp) {
                    ret = mpp_buffer_group_get_internal(&frm_grp, MPP_BUFFER_TYPE_ION);
                    if (ret != MPP_OK) {
                        fprintf(stderr, "mpp_buffer_group_get_internal failed ret:%d\n", ret);
                        goto MPP_DECODE_OUT;
                    }
                    // 设置缓冲区限制（JPEG 解码 4 个缓冲区足够）
                    mpp_buffer_group_limit_config(frm_grp, buf_size, 4);
                    // 将缓冲区组设置到解码器
                    mpi->control(ctx, MPP_DEC_SET_EXT_BUF_GROUP, frm_grp);
                }
                // 通知解码器信息变更完成
                mpi->control(ctx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);
                continue;
            }

            // 7. 保存 YUV 数据到文件
            if (frame && !mpp_frame_get_errinfo(frame)) {
                save_yuv_to_file(frame, output_yuv);
            }

            // 释放帧资源
            if (frame) {
                mpp_frame_deinit(&frame);
                frame = NULL;
            }
            break;
        }
    } while (1);

MPP_DECODE_OUT:
    // 资源释放
    if (packet) mpp_packet_deinit(&packet);
    if (frame) mpp_frame_deinit(&frame);
    if (frm_grp) mpp_buffer_group_put(frm_grp);
    if (ctx) mpp_destroy(ctx);
    if (jpg_data) mpp_free(jpg_data);

    return ret == MPP_OK ? 0 : -1;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.jpg> <output.yuv>\n", argv[0]);
        return -1;
    }

    const char* input_jpg = argv[1];
    const char* output_yuv = argv[2];

    int ret = mpp_jpeg_decode(input_jpg, output_yuv);
    if (ret == 0) {
        printf("JPEG decode success!\n");
    } else {
        fprintf(stderr, "JPEG decode failed!\n");
    }

    return ret;
}