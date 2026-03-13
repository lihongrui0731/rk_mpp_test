#include <rockchip/rk_mpi.h>
#include <stdio.h>
#include <stdlib.h>
#define PRINT_LINE	printf("xxxxxx %d \n",__LINE__);
int main() {
    MppCtx ctx = NULL;
    MppApi *mpi = NULL;
    MppBufferGroup buf_grp = NULL;
    MppPacket packet = NULL;
    MppFrame frame = NULL;

    // 1. 初始化 MPP 上下文
    MPP_RET ret = mpp_create(&ctx, &mpi);
    if (ret != MPP_OK) {
        printf("mpp_create failed\n");
        return -1;
    }
PRINT_LINE
    // 2. 创建 JPEG 解码器
    ret = mpp_init(ctx, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG);
    if (ret != MPP_OK) {
        printf("mpp_init failed\n");
        goto EXIT;
    }
PRINT_LINE
    // 3. 准备输入数据（假设 jpeg_data 是 JPEG 文件内容）
    FILE *fp = fopen("input.jpg", "rb");
    fseek(fp, 0, SEEK_END);
    size_t file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint8_t *jpeg_data = malloc(file_size);
    fread(jpeg_data, 1, file_size, fp);
    fclose(fp);
	printf("file_size :%d \n",file_size);
PRINT_LINE
    // 4. 创建输入 Packet
    ret = mpp_packet_init(&packet, jpeg_data, file_size);
    if (ret != MPP_OK) {
        printf("mpp_packet_init failed\n");
        goto EXIT;
    }
    mpp_packet_set_eos(packet); // 标记数据结束
PRINT_LINE
    // 5. 解码循环
    while (1) {
        // 送入数据
        ret = mpi->decode_put_packet(ctx, packet);
        if (ret != MPP_OK) break;
PRINT_LINE
        // 获取解码后的帧
        ret = mpi->decode_get_frame(ctx, &frame);

printf("xxxxxxxx%s  %d    ret:%d  frame:%d  \n",__func__,__LINE__,ret,frame);
        if (ret != MPP_OK || !frame) break;

     PRINT_LINE   // 检查帧状态
        if (mpp_frame_get_info_change(frame)) {
            // 处理格式变化（如分辨率更新）
            int width = mpp_frame_get_width(frame);
            int height = mpp_frame_get_height(frame);
            printf("Resolution: %dx%d\n", width, height);
            continue;
        }
PRINT_LINE
        // 获取解码数据（YUV 格式）
        void *buf = mpp_frame_get_buffer(frame);
        if (buf) {
            // 处理解码后的 YUV 数据（保存或渲染）
            // ...
        }
        mpp_frame_deinit(&frame); // 释放帧
		PRINT_LINE
    }

EXIT:
    // 6. 释放资源
    if (packet) mpp_packet_deinit(&packet);
    if (frame) mpp_frame_deinit(&frame);
    if (ctx) mpp_destroy(ctx);
    free(jpeg_data);
    return 0;
}