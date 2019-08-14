#pragma GCC diagnostic ignored "-Wunused-private-field"
#pragma GCC diagnostic ignored "-Wunused-variable"

extern "C" {
#include <libavformat/avformat.h>
}
#include <libavutil/timestamp.h>
#include <stdlib.h>
#include <string>

#ifdef OS_LINUX
#include <filesystem>
namespace fs = std::filesystem;
#endif
#include "inc/blockingconcurrentqueue.hpp"

using namespace std;
void avlogThrow(void * avcl, int lvl, const char *fmt, ...) {
    (void) avcl;
    (void) lvl;
    va_list args;
    va_start( args, fmt );
    av_log(NULL, AV_LOG_ERROR, fmt, args);
    va_end( args );
    throw fmt;
}

int main(int argc, char **argv)
{
    AVFormatContext *pAVFormatInput = NULL, *pAVFormatRemux = NULL;
    AVPacket packet;
    AVCodec *pCodec = NULL;
    AVDictionary *pOptsRemux = NULL, *pOptsInput = NULL, *pOptsOutput = NULL;
    (void) pOptsInput;
    const char *urlInput, *urlOutput;
    int ret, i, idxVideo;
    int streamIdx = 0;
    int *streamList = NULL;
    int numStreams = 0;


    if (argc != 3) {
        printf("usage: <cmd> rtsp_in rtsp_out\n");
        return -1;
    }

    urlInput  = argv[1];
    urlOutput = argv[2];

    if ((ret = avformat_open_input(&pAVFormatInput, urlInput, NULL, NULL)) < 0) {
        avlogThrow(NULL, AV_LOG_ERROR,  "Could not open input file '%s'", urlInput);
        goto end;
    }
    if ((ret = avformat_find_stream_info(pAVFormatInput, NULL)) < 0) {
        avlogThrow(NULL, AV_LOG_ERROR,  "Failed to retrieve input stream information");
        goto end;
    }

    ret = avformat_alloc_output_context2(&pAVFormatRemux, NULL, "rtsp", urlOutput);
    if (ret < 0) {
        avlogThrow(NULL, AV_LOG_ERROR, "failed create avformatcontext for output: %s", av_err2str(ret));
        goto end;
    }

    numStreams = pAVFormatInput->nb_streams;
    streamList = (int *)av_mallocz_array(numStreams, sizeof(*streamList));

    if (!streamList) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    // find all video & audio streams for remuxing
    for (i = 0; i < pAVFormatInput->nb_streams; i++) {
        AVStream *out_stream;
        AVStream *in_stream = pAVFormatInput->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;
        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
                in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
                in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            streamList[i] = -1;
            continue;
        }
        streamList[i] = streamIdx++;
        out_stream = avformat_new_stream(pAVFormatRemux, NULL);
        if (!out_stream) {
            avlogThrow(NULL, AV_LOG_ERROR,  "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }
        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0) {
            avlogThrow(NULL, AV_LOG_ERROR,  "Failed to copy codec parameters\n");
            goto end;
        }
    }

    av_dump_format(pAVFormatRemux, 0, urlOutput, 1);

    // find best video stream
    idxVideo = av_find_best_stream(pAVFormatInput, AVMEDIA_TYPE_VIDEO, -1, -1, &pCodec, 0);
    if(idxVideo < 0) {
        avlogThrow(NULL, AV_LOG_ERROR, "failed find best video stream");
        goto end;
    }


    // unless it's a no file (we'll talk later about that) write to the disk (FLAG_WRITE)
    // but basically it's a way to save the file to a buffer so you can store it
    // wherever you want.

    if (!(pAVFormatRemux->oformat->flags & AVFMT_NOFILE)) {
        avlogThrow(NULL, AV_LOG_ERROR,  "Failed allocating output stream\n");
        ret = avio_open2(&pAVFormatRemux->pb, urlOutput, AVIO_FLAG_WRITE, NULL, &pOptsRemux);
        if (ret < 0) {
            avlogThrow(NULL, AV_LOG_ERROR,  "Could not open output file '%s'", urlOutput);
            goto end;
        }
    }

    // if (mp4Fragmented) {
    //   av_dict_set(&pOptsRemux, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
    // }

    // rtsp tcp
    if(av_dict_set(&pOptsRemux, "rtsp_transport", "tcp", 0) < 0) {
        avlogThrow(NULL, AV_LOG_ERROR, "failed set output pOptsRemux");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    ret = avformat_write_header(pAVFormatRemux, &pOptsRemux);
    if (ret < 0) {
        avlogThrow(NULL, AV_LOG_ERROR,  "Error occurred when opening output file\n");
        goto end;
    }
    while (1) {
        AVStream *in_stream, *out_stream;
        ret = av_read_frame(pAVFormatInput, &packet);
        if (ret < 0)
            break;
        in_stream  = pAVFormatInput->streams[packet.stream_index];
        if (packet.stream_index >= numStreams || streamList[packet.stream_index] < 0) {
            av_packet_unref(&packet);
            continue;
        }

        av_log(NULL, AV_LOG_ERROR, "pts: %lld, dts:%lld, dur: %lld\n", packet.pts, packet.dts, packet.duration);
        packet.stream_index = streamList[packet.stream_index];
        out_stream = pAVFormatRemux->streams[packet.stream_index];
        /* copy packet */
        packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);
        packet.pos = -1;
        av_log(NULL, AV_LOG_WARNING, "pts: %lld, dts:%lld, dur: %lld, idx: %d\n", packet.pts, packet.dts, packet.duration, packet.stream_index);


        ret = av_interleaved_write_frame(pAVFormatRemux, &packet);
        if (ret < 0) {
            avlogThrow(NULL, AV_LOG_ERROR,  "Error muxing packet\n");
            break;
        }
        av_packet_unref(&packet);
    }

    av_write_trailer(pAVFormatRemux);
end:
    avformat_close_input(&pAVFormatInput);
    /* close output */
    if (pAVFormatRemux && !(pAVFormatRemux->oformat->flags & AVFMT_NOFILE))
        avio_closep(&pAVFormatRemux->pb);
    avformat_free_context(pAVFormatRemux);
    av_freep(&streamList);
    if (ret < 0 && ret != AVERROR_EOF) {
        avlogThrow(NULL, AV_LOG_ERROR,  "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }
    return 0;
}

