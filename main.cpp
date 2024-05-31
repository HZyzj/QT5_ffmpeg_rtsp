#include <QApplication>
#include <QWidget>
#include <QTimer>
#include <QImage>
#include <QLabel>
#include <QVBoxLayout>
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

class VideoWidget : public QWidget {
    Q_OBJECT

public:
    VideoWidget(const char* rtspUrl, QWidget* parent = nullptr);
    ~VideoWidget();

private slots:
    void updateFrame();

private:
    QLabel* label;
    QTimer* timer;
    AVFormatContext* formatContext;
    AVCodecContext* codecContext;
    AVFrame* frame;
    SwsContext* swsCtx;
    int videoStreamIndex;

    void initFFmpeg(const char* rtspUrl);
    void cleanupFFmpeg();
};

VideoWidget::VideoWidget(const char* rtspUrl, QWidget* parent)
    : QWidget(parent), formatContext(nullptr), codecContext(nullptr), frame(nullptr), swsCtx(nullptr), videoStreamIndex(-1) {
    label = new QLabel(this);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(label);
    setLayout(layout);

    initFFmpeg(rtspUrl);

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &VideoWidget::updateFrame);
    timer->start(30); // Update frame every 30ms
}

VideoWidget::~VideoWidget() {
    cleanupFFmpeg();
}

void VideoWidget::initFFmpeg(const char* rtspUrl) {
    av_register_all();
    avformat_network_init();

    if (avformat_open_input(&formatContext, rtspUrl, nullptr, nullptr) != 0) {
        std::cerr << "Could not open input file.\n";
        return;
    }

    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        std::cerr << "Could not find stream information.\n";
        return;
    }

    for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            break;
        }
    }

    if (videoStreamIndex == -1) {
        std::cerr << "Could not find video stream.\n";
        return;
    }

    AVCodecParameters* codecParams = formatContext->streams[videoStreamIndex]->codecpar;
    AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        std::cerr << "Unsupported codec.\n";
        return;
    }

    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext) {
        std::cerr << "Could not allocate codec context.\n";
        return;
    }

    if (avcodec_parameters_to_context(codecContext, codecParams) < 0) {
        std::cerr << "Could not copy codec context.\n";
        return;
    }

    if (avcodec_open2(codecContext, codec, nullptr) < 0) {
        std::cerr << "Could not open codec.\n";
        return;
    }

    frame = av_frame_alloc();
    if (!frame) {
        std::cerr << "Could not allocate frame.\n";
        return;
    }

    swsCtx = sws_getContext(codecContext->width, codecContext->height, codecContext->pix_fmt,
                            codecContext->width, codecContext->height, AV_PIX_FMT_RGB24,
                            SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!swsCtx) {
        std::cerr << "Could not initialize SWS context.\n";
        return;
    }
}

void VideoWidget::cleanupFFmpeg() {
    if (frame) av_frame_free(&frame);
    if (codecContext) avcodec_free_context(&codecContext);
    if (formatContext) avformat_close_input(&formatContext);
    if (swsCtx) sws_freeContext(swsCtx);
}

void VideoWidget::updateFrame() {
    AVPacket packet;
    av_init_packet(&packet);
    packet.data = nullptr;
    packet.size = 0;

    if (av_read_frame(formatContext, &packet) >= 0) {
        if (packet.stream_index == videoStreamIndex) {
            int ret = avcodec_send_packet(codecContext, &packet);
            if (ret >= 0) {
                ret = avcodec_receive_frame(codecContext, frame);
                if (ret >= 0) {
                    AVFrame* rgbFrame = av_frame_alloc();

                    int numBytes = avpicture_get_size(AV_PIX_FMT_RGB24, codecContext->width, codecContext->height);
                    uint8_t* buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));

                    avpicture_fill((AVPicture*)rgbFrame, buffer, AV_PIX_FMT_RGB24, codecContext->width, codecContext->height);

                    sws_scale(swsCtx, (uint8_t const* const*)frame->data, frame->linesize, 0, codecContext->height, rgbFrame->data, rgbFrame->linesize);

                    QImage img(rgbFrame->data[0], codecContext->width, codecContext->height, QImage::Format_RGB888);
                    label->setPixmap(QPixmap::fromImage(img));

                    av_free(buffer);
                    av_frame_free(&rgbFrame);
                }
            }
        }
        av_packet_unref(&packet);
    }
}


int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    const char* rtspUrl = "rtsp://admin:admin@192.168.1.49/0";
    VideoWidget widget(rtspUrl);
    widget.show();
    return app.exec();
}

#include "main.moc"
