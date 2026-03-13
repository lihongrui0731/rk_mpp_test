#ifndef __MPP_JPEG_DEC_H__
#define __MPP_JPEG_DEC_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int mpp_decode_jpeg_stream(char *jpeg_data, size_t jpeg_size, size_t jpeg_width, size_t jpeg_height, char *yuv_data, size_t yuv_size, size_t *yuv_width, size_t *yuv_height);

#ifdef __cplusplus
}
#endif

#endif // __MPP_JPEG_DEC_H__
