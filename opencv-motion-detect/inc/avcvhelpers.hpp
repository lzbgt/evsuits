#ifndef __AVCVHELPERS_H__
#define __AVCVHELPERS_H__
extern "C" {
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>
}

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <map>
using namespace std;

namespace avcvhelpers {
// AVFrame mat2frame(cv::Mat* frame)
// {
//     // TODO.
//         AVFrame dst;
//         // cv::Size frameSize = frame->size();
//         // AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_RAWVIDEO);
//         // AVFormatContext* outContainer = avformat_alloc_context();
//         // AVStream *outStream = avformat_new_stream(outContainer, encoder);

//         // // outStream->codec->pix_fmt = AV_PIX_FMT_BGR24;
//         // outStream->codecpar->format = AV_PIX_FMT_BGR24;
//         // outStream->codecpar->width = frame->cols;
//         // outStream->codecpar->height = frame->rows;
//         // avpicture_fill((AVPicture*)&dst, frame->data, AV_PIX_FMT_BGR24, outStream->codecpar->width, outStream->codecpar->height);
//         // dst.width = frameSize.width;
//         // dst.height = frameSize.height;

//         return dst;
// }

// struct Point {
//     int format;
//     int width;
//     int height;
//     bool operator==(Point const &other) {
//         return format == other.format && width == other.width && height == other.height;
//     }

//     size_t operator()(Point const &pt) {
//         size_t h = hash<int>{}(pt.format);
//         h ^= (hash<int>{}(pt.width) <<1);
//         h ^= (hash<int>{}(pt.height) <<1);
//         return h;
//     }
// };

void frame2mat(AVPixelFormat format, const AVFrame * frame, cv::Mat& image)
{
    int width = frame->width;
    int height = frame->height;

    // Allocate the opencv mat and store its stride in a 1-element array
    if (image.rows != height || image.cols != width || image.type() != CV_8UC3) {
        image = cv::Mat(height, width, CV_8UC3);
    }

    int cvLinesizes[1];
    cvLinesizes[0] = image.step1();

    // Convert the colour format and write directly to the opencv matrix
    // TODO: optimization
    switch (format) {
    case AV_PIX_FMT_YUVJ420P:
        format = AV_PIX_FMT_YUV420P;
        break;
    case AV_PIX_FMT_YUVJ422P:
        format = AV_PIX_FMT_YUV422P;
        break;
    case AV_PIX_FMT_YUVJ444P:
        format = AV_PIX_FMT_YUV444P;
        break;
    case AV_PIX_FMT_YUVJ440P:
        format = AV_PIX_FMT_YUV440P;
        break;
    default:
        ;
    }
    SwsContext* conversion = sws_getContext(width, height, (AVPixelFormat) format, width, height, AVPixelFormat::AV_PIX_FMT_BGR24, SWS_FAST_BILINEAR, NULL, NULL, NULL);
    sws_scale(conversion, frame->data, frame->linesize, 0, height, &image.data, cvLinesizes);
    sws_freeContext(conversion);
}
}

#endif
