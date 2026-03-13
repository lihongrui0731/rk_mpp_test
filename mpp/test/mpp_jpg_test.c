#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <rockchip/rk_mpi.h>

void process_frame(MppFrame frame);
void save_yuv(MppFrame frame, const char *filename);

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <input.jpg> [output.yuv]\n", argv[0]);
        return -1;
    }
    
    const char *input_file = argv[1];
    const char *output_file = (argc > 2) ? argv[2] : "output.yuv";
    
    // 1. 初始化 MPP
    MppCtx ctx = NULL;
    MppApi *mpi = NULL;
    MPP_RET ret = mpp_create(&ctx, &mpi);
    if (ret != MPP_OK) {
        printf("mpp_create failed: %d\n", ret);
        return -1;
    }
    
    ret = mpp_init(ctx, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG);
    if (ret != MPP_OK) {
        printf("mpp_init failed: %d\n", ret);
        mpp_destroy(ctx);
        return -1;
    }
    
    // 2. 读取输入文件
    FILE *fp = fopen(input_file, "rb");
    if (!fp) {
        printf("无法打开输入文件: %s\n", input_file);
        mpp_destroy(ctx);
        return -1;
    }
    
    fseek(fp, 0, SEEK_END);
    size_t file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    uint8_t *jpeg_data = malloc(file_size);
    fread(jpeg_data, 1, file_size, fp);
    fclose(fp);
    
    // 3. 创建输入包
    MppPacket packet = NULL;
    mpp_packet_init(&packet, jpeg_data, file_size);
    mpp_packet_set_pts(packet, 0);
    
    // 4. 解码循环
    ret = mpi->decode_put_packet(ctx, packet);
    if (ret != MPP_OK) {
        printf("decode_put_packet failed: %d\n", ret);
        goto exit;
    }
    
    int frame_count = 0;
    while (1) {
        MppFrame frame = NULL;
        ret = mpi->decode_get_frame(ctx, &frame);
        
        if (ret != MPP_OK || !frame) {
            usleep(5000);
            continue;
        }
        
        if (mpp_frame_get_info_change(frame)) {
            int width = mpp_frame_get_width(frame);
            int height = mpp_frame_get_height(frame);
            printf("格式变化: %dx%d\n", width, height);
            mpp_frame_deinit(&frame);
            continue;
        }
        
        if (mpp_frame_get_eos(frame)) {
            printf("流结束\n");
            mpp_frame_deinit(&frame);
            break;
        }
        
        // 处理解码帧
        frame_count++;
        printf("解码帧 #%d\n", frame_count);
        save_yuv(frame, output_file);
        
        mpp_frame_deinit(&frame);
    }
    
    printf("成功解码 %d 帧\n", frame_count);
    
exit:
    // 5. 清理资源
    mpp_packet_deinit(&packet);
    free(jpeg_data);
    mpp_destroy(ctx);
    return 0;
}

void save_yuv(MppFrame frame, const char *filename) {
    MppBuffer buffer = mpp_frame_get_buffer(frame);
    if (!buffer) {
        printf("帧中没有缓冲区\n");
        return;
    }
    int frame_count=1;
    uint8_t *yuv_data = (uint8_t *)mpp_buffer_get_ptr(buffer);
    size_t data_size = mpp_buffer_get_size(buffer);
    
    FILE *out = fopen(filename, frame_count == 1 ? "wb" : "ab");
    if (!out) {
        printf("无法打开输出文件: %s\n", filename);
        return;
    }
    
    fwrite(yuv_data, 1, data_size, out);
    fclose(out);
}