#ifndef MPP_OSD_WATERMARK_H
#define MPP_OSD_WATERMARK_H

#include <QString>
#include <QList>
#include <QImage>
#include <QPainter>

#include "rk_mpi.h"
#include "rk_venc_cmd.h"
#include "mpp_buffer.h"

class MppOsdWatermark {
public:
    MppOsdWatermark();
    ~MppOsdWatermark();

    // Initialize with the encoder context, API, and an optional buffer group.
    // If buf_group is NULL, an internal buffer group will be created.
    int init(MppCtx ctx, MppApi *mpi, MppBufferGroup buf_group = nullptr);

    // Set the watermark text lines.
    // The text will be rendered and OSD data will be prepared internally.
    int setWatermarkText(const QList<QString>& textLines);

    // Apply the configured OSD data to the encoder context.
    // This calls mpi->control(ctx, MPP_ENC_SET_OSD_DATA_CFG, ...)
    int applyWatermark();

private:
    void releaseOsdBuffer();
    int setupPalette();

    MppCtx m_ctx;
    MppApi *m_mpi;
    MppBufferGroup m_buf_group;
    bool m_own_buf_group;

    MppBuffer m_osd_buf;
    MppEncOSDData m_osd_data;

    QList<QString> m_textLines;
    bool m_osd_ready;
};

#endif // MPP_OSD_WATERMARK_H
