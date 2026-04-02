#include "mpp_osd_watermark.h"
#include <QFont>
#include <QFontMetrics>
#include <string.h>

#define MPP_ALIGN(x, a)         (((x)+(a)-1)&~((a)-1))

MppOsdWatermark::MppOsdWatermark()
    : m_ctx(nullptr),
      m_mpi(nullptr),
      m_buf_group(nullptr),
      m_own_buf_group(false),
      m_osd_buf(nullptr),
      m_osd_ready(false)
{
    memset(&m_osd_data, 0, sizeof(m_osd_data));
}

MppOsdWatermark::~MppOsdWatermark()
{
    releaseOsdBuffer();

    if (m_own_buf_group && m_buf_group) {
        mpp_buffer_group_put(m_buf_group);
        m_buf_group = nullptr;
    }
}

int MppOsdWatermark::init(MppCtx ctx, MppApi *mpi, MppBufferGroup buf_group)
{
    if (!ctx || !mpi) {
        return -1;
    }

    m_ctx = ctx;
    m_mpi = mpi;

    if (buf_group) {
        m_buf_group = buf_group;
        m_own_buf_group = false;
    } else {
        MPP_RET ret = mpp_buffer_group_get_internal(&m_buf_group, MPP_BUFFER_TYPE_ION);
        if (ret != MPP_OK) {
            return -1;
        }
        m_own_buf_group = true;
    }

    return setupPalette();
}

int MppOsdWatermark::setupPalette()
{
    // Setup OSD Palette.
    // Index 0: Transparent (V=0, U=0, Y=0, Alpha=0) -> Alpha=0 means full transparent in typical OSD config
    // Index 1: White (Alpha=255)

    MppEncOSDPltCfg plt_cfg;
    memset(&plt_cfg, 0, sizeof(plt_cfg));

    MppEncOSDPlt plt;
    memset(&plt, 0, sizeof(plt));

    // Color 0: Transparent
    plt.data[0].alpha = 0;
    plt.data[0].y = 16;
    plt.data[0].u = 128;
    plt.data[0].v = 128;

    // Color 1: Solid White
    plt.data[1].alpha = 255;
    plt.data[1].y = 235;
    plt.data[1].u = 128;
    plt.data[1].v = 128;

    // Color 2: Solid Black (for outline/shadow if needed)
    plt.data[2].alpha = 255;
    plt.data[2].y = 16;
    plt.data[2].u = 128;
    plt.data[2].v = 128;

    plt_cfg.change = MPP_ENC_OSD_PLT_CFG_CHANGE_ALL;
    plt_cfg.type = MPP_ENC_OSD_PLT_TYPE_USERDEF;
    plt_cfg.plt = &plt;

    MPP_RET ret = m_mpi->control(m_ctx, MPP_ENC_SET_OSD_PLT_CFG, &plt_cfg);
    if (ret != MPP_OK) {
        return -1;
    }

    return 0;
}

void MppOsdWatermark::releaseOsdBuffer()
{
    if (m_osd_buf) {
        mpp_buffer_put(m_osd_buf);
        m_osd_buf = nullptr;
    }
}

int MppOsdWatermark::setWatermarkText(const QList<QString>& textLines)
{
    if (!m_ctx || !m_mpi || !m_buf_group) {
        return -1;
    }

    m_textLines = textLines;
    m_osd_ready = false;

    if (textLines.isEmpty()) {
        memset(&m_osd_data, 0, sizeof(m_osd_data));
        m_osd_ready = true;
        return 0;
    }

    // Determine font and size
    QFont font("Arial", 20, QFont::Bold);
    QFontMetrics fm(font);

    int text_w = 0;
    int text_h = fm.height() * textLines.size();

    for (const QString& line : textLines) {
        int w = fm.horizontalAdvance(line);
        if (w > text_w) {
            text_w = w;
        }
    }

    if (text_w == 0 || text_h == 0) {
        return 0;
    }

    // OSD requires the width and height to be aligned to 16 pixels (macroblock size)
    int align_w = MPP_ALIGN(text_w, 16);
    int align_h = MPP_ALIGN(text_h, 16);

    // Render text to QImage
    QImage image(align_w, align_h, QImage::Format_Grayscale8);
    image.fill(0); // Fill with index 0 (Transparent)

    QPainter painter(&image);
    painter.setFont(font);

    // Draw text with index 1 (White)
    painter.setPen(QColor(1, 1, 1));

    int y = fm.ascent();
    for (const QString& line : textLines) {
        painter.drawText(0, y, line);
        y += fm.height();
    }
    painter.end();

    // Allocate MPP Buffer for OSD data
    size_t osd_sz = align_w * align_h;

    releaseOsdBuffer();

    MPP_RET ret = mpp_buffer_get(m_buf_group, &m_osd_buf, osd_sz);
    if (ret != MPP_OK) {
        return -1;
    }

    void* ptr = mpp_buffer_get_ptr(m_osd_buf);
    if (!ptr) {
        releaseOsdBuffer();
        return -1;
    }

    // Copy QImage pixel data (which contains 0 and 1) to MPP buffer
    for (int r = 0; r < align_h; ++r) {
        memcpy((uint8_t*)ptr + r * align_w, image.scanLine(r), align_w);
    }

    // Configure MppEncOSDData
    memset(&m_osd_data, 0, sizeof(m_osd_data));
    m_osd_data.buf = m_osd_buf;
    m_osd_data.num_region = 1;

    m_osd_data.region[0].enable = 1;
    m_osd_data.region[0].inverse = 0;
    // Put at top-left corner
    m_osd_data.region[0].start_mb_x = 0;
    m_osd_data.region[0].start_mb_y = 0;
    m_osd_data.region[0].num_mb_x = align_w / 16;
    m_osd_data.region[0].num_mb_y = align_h / 16;
    m_osd_data.region[0].buf_offset = 0;

    m_osd_ready = true;
    return 0;
}

int MppOsdWatermark::applyWatermark()
{
    if (!m_ctx || !m_mpi || !m_osd_ready) {
        return -1;
    }

    // When num_region is 0, it means clear the OSD.
    MPP_RET ret = m_mpi->control(m_ctx, MPP_ENC_SET_OSD_DATA_CFG, &m_osd_data);
    if (ret != MPP_OK) {
        return -1;
    }

    return 0;
}
