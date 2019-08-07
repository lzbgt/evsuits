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

#include "inc/json.hpp"
#include "inc/blockingconcurrentqueue.hpp"

using namespace std;
using json = nlohmann::json;
using namespace moodycamel;

class VideoProcessor {
private:
#define SECS_SLICE (60*5/2)
    AVFormatContext *pAVFormatInput = NULL, *pAVFormatRemux = NULL, *pAVFormatSlice = NULL;
    AVCodec *pCodec = NULL;
    AVDictionary *pOptsRemux = NULL, *pOptsInput = NULL, *pOptsOutput = NULL;
    int idxVideo = -1, idxAudio = -1, numStreams = 0, numSlices = 6, secsSlice = SECS_SLICE;
    int *streamList = NULL;
    bool bPush = true, bRecord = false;
    string urlIn, urlOut, pathSlice;
    unordered_map<string, string> envParams = unordered_map<string, string>();
    BlockingConcurrentQueue<AVPacket*> remuxPktQueue;
    BlockingConcurrentQueue<AVPacket*> slicePktQueue;
    BlockingConcurrentQueue<AVPacket*> framePktQueue;
private:
    void logThrow(void * avcl, int lvl, const char *fmt, ...) {
        (void) avcl;
        (void) lvl;
        va_list args;
        va_start( args, fmt );
        av_log(NULL, AV_LOG_ERROR, fmt, args);
        va_end( args );
        throw fmt;
    }

    void setupParams()
    {
        char *tmp = getenv("URL_IN");
        urlIn = (tmp == NULL?string(""): string(tmp));

        tmp= getenv("URL_OUT");
        urlOut = (tmp == NULL?string(""): string(tmp));

        tmp = getenv("SLICE_NUM");
        numSlices = (tmp == NULL?6:atoi(tmp));
        if(numSlices <=2) {
            numSlices = 6;
        }

        tmp = getenv("SLICE_PATH");
        pathSlice = (tmp == NULL?string("slices"):string(tmp));
        
        // OSX XCode doesn't ship with the filesystem header as of version 10.x
        #ifdef __LINUX___
            if (!fs::exists(pathSlice.c_str())) {
                if (!fs::create_directory(pathSlice.c_str())) {
                    logThrow(NULL, AV_LOG_ERROR, "can't create directory: %s", pathSlice.c_str());
                    exit(1);
                }
                fs::permissions(pathSlice.c_str(), fs::perms::all);
            }
        #endif


        tmp = getenv("PUSH");
        bPush = (tmp == NULL?false: (string(tmp) == string("false")?false:true));

        tmp = getenv("SLICE_SECS");
        secsSlice = (tmp == NULL?SECS_SLICE:atoi(tmp));
        if(secsSlice < SECS_SLICE) {
            secsSlice = SECS_SLICE;
        }

        if(urlIn == "" or urlOut == "") {
            logThrow(NULL, AV_LOG_ERROR, "no input/output url");
            exit(1);
        }
    }

    int setupStreams()
    {
        int ret = 0, i, streamIdx = 0;
        if ((ret = avformat_open_input(&pAVFormatInput, urlIn.c_str(), NULL, NULL)) < 0) {
            logThrow(NULL, AV_LOG_ERROR,  "Could not open input file '%s'", urlIn.c_str());
        }
        if ((ret = avformat_find_stream_info(pAVFormatInput, NULL)) < 0) {
            logThrow(NULL, AV_LOG_ERROR,  "Failed to retrieve input stream information");
        }

        pAVFormatInput->flags = AVFMT_FLAG_NOBUFFER | AVFMT_FLAG_FLUSH_PACKETS;

        ret = avformat_alloc_output_context2(&pAVFormatRemux, NULL, "rtsp", urlOut.c_str());
        if (ret < 0) {
            logThrow(NULL, AV_LOG_ERROR, "failed create avformatcontext for output: %s", av_err2str(ret));
        }

        numStreams = pAVFormatInput->nb_streams;
        streamList = (int *)av_mallocz_array(numStreams, sizeof(*streamList));

        if (!streamList) {
            ret = AVERROR(ENOMEM);
            logThrow(NULL, AV_LOG_ERROR, "failed create avformatcontext for output: %s", av_err2str(AVERROR(ENOMEM)));
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
                logThrow(NULL, AV_LOG_ERROR,  "Failed allocating output stream\n");
                ret = AVERROR_UNKNOWN;

            }
            ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
            if (ret < 0) {
                logThrow(NULL, AV_LOG_ERROR,  "Failed to copy codec parameters\n");
            }
        }

        av_dump_format(pAVFormatRemux, 0, urlOut.c_str(), 1);

        // find best video stream
        idxVideo = av_find_best_stream(pAVFormatInput, AVMEDIA_TYPE_VIDEO, -1, -1, &pCodec, 0);
        if(idxVideo < 0) {
            logThrow(NULL, AV_LOG_ERROR, "failed find best video stream");
        }
        
        if (!(pAVFormatRemux->oformat->flags & AVFMT_NOFILE)) {
            logThrow(NULL, AV_LOG_ERROR,  "Failed allocating output stream\n");
            ret = avio_open2(&pAVFormatRemux->pb, urlOut.c_str(), AVIO_FLAG_WRITE, NULL, &pOptsRemux);
            if (ret < 0) {
                logThrow(NULL, AV_LOG_ERROR,  "Could not open output file '%s'", urlOut.c_str());
            }
        }

        // rtsp tcp
        if(av_dict_set(&pOptsRemux, "rtsp_transport", "tcp", 0) < 0) {
            logThrow(NULL, AV_LOG_ERROR, "failed set output pOptsRemux");
            ret = AVERROR_UNKNOWN;
        }

        ret = avformat_write_header(pAVFormatRemux, &pOptsRemux);
        if (ret < 0) {
            logThrow(NULL, AV_LOG_ERROR,  "Error occurred when opening output file\n");
        }
        while (1) {
            AVStream *in_stream, *out_stream;
            AVPacket packet;
            ret = av_read_frame(pAVFormatInput, &packet);
            if (ret < 0)
                break;
            in_stream  = pAVFormatInput->streams[packet.stream_index];
            if (packet.stream_index >= numStreams || streamList[packet.stream_index] < 0) {
                av_packet_unref(&packet);
                continue;
            }
            packet.stream_index = streamList[packet.stream_index];
            out_stream = pAVFormatRemux->streams[packet.stream_index];
            /* copy packet */
            packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);
            packet.pos = -1;

            ret = av_interleaved_write_frame(pAVFormatRemux, &packet);
            if (ret < 0) {
                logThrow(NULL, AV_LOG_ERROR,  "Error muxing packet\n");
                break;
            }
            av_packet_unref(&packet);
        }

        av_write_trailer(pAVFormatRemux);
        return ret;
    }

public:
    // ctor
    VideoProcessor()
    {
        setupParams();
        setupStreams();
    }
    // dtor
    ~VideoProcessor() {
        avformat_close_input(&pAVFormatInput);
        /* close output */
        if (pAVFormatRemux && !(pAVFormatRemux->oformat->flags & AVFMT_NOFILE))
            avio_closep(&pAVFormatRemux->pb);
        avformat_free_context(pAVFormatRemux);
        av_freep(&streamList);
    }
};


int main(int argc, char **argv)
{
  auto vp = VideoProcessor();


//     AVFormatContext *pAVFormatInput = NULL, *pAVFormatRemux = NULL;
//     AVPacket packet;
//     AVCodec *pCodec = NULL;
//     AVDictionary *pOptsRemux = NULL, *pOptsInput = NULL, *pOptsOutput = NULL;
//     (void) pOptsInput;
//     const char *urlInput, *urlOutput;
//     int ret, i, idxVideo;
//     int streamIdx = 0;
//     int *streamList = NULL;
//     int numStreams = 0;


//     if (argc != 3) {
//         printf("usage: <cmd> rtsp_in rtsp_out\n");
//         return -1;
//     }

//     urlInput  = argv[1];
//     urlOutput = argv[2];

//     if ((ret = avformat_open_input(&pAVFormatInput, urlInput, NULL, NULL)) < 0) {
//         logThrow(NULL, AV_LOG_ERROR,  "Could not open input file '%s'", urlInput);
//         goto end;
//     }
//     if ((ret = avformat_find_stream_info(pAVFormatInput, NULL)) < 0) {
//         logThrow(NULL, AV_LOG_ERROR,  "Failed to retrieve input stream information");
//         goto end;
//     }

//     ret = avformat_alloc_output_context2(&pAVFormatRemux, NULL, "rtsp", urlOutput);
//     if (ret < 0) {
//         logThrow(NULL, AV_LOG_ERROR, "failed create avformatcontext for output: %s", av_err2str(ret));
//         goto end;
//     }

//     numStreams = pAVFormatInput->nb_streams;
//     streamList = (int *)av_mallocz_array(numStreams, sizeof(*streamList));

//     if (!streamList) {
//         ret = AVERROR(ENOMEM);
//         goto end;
//     }

//     // find all video & audio streams for remuxing
//     for (i = 0; i < pAVFormatInput->nb_streams; i++) {
//         AVStream *out_stream;
//         AVStream *in_stream = pAVFormatInput->streams[i];
//         AVCodecParameters *in_codecpar = in_stream->codecpar;
//         if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
//                 in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
//                 in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
//             streamList[i] = -1;
//             continue;
//         }
//         streamList[i] = streamIdx++;
//         out_stream = avformat_new_stream(pAVFormatRemux, NULL);
//         if (!out_stream) {
//             logThrow(NULL, AV_LOG_ERROR,  "Failed allocating output stream\n");
//             ret = AVERROR_UNKNOWN;
//             goto end;
//         }
//         ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
//         if (ret < 0) {
//             logThrow(NULL, AV_LOG_ERROR,  "Failed to copy codec parameters\n");
//             goto end;
//         }
//     }

//     av_dump_format(pAVFormatRemux, 0, urlOutput, 1);

//     // find best video stream
//     idxVideo = av_find_best_stream(pAVFormatInput, AVMEDIA_TYPE_VIDEO, -1, -1, &pCodec, 0);
//     if(idxVideo < 0) {
//         logThrow(NULL, AV_LOG_ERROR, "failed find best video stream");
//         goto end;
//     }


//     // unless it's a no file (we'll talk later about that) write to the disk (FLAG_WRITE)
//     // but basically it's a way to save the file to a buffer so you can store it
//     // wherever you want.

//     if (!(pAVFormatRemux->oformat->flags & AVFMT_NOFILE)) {
//         logThrow(NULL, AV_LOG_ERROR,  "Failed allocating output stream\n");
//         ret = avio_open2(&pAVFormatRemux->pb, urlOutput, AVIO_FLAG_WRITE, NULL, &pOptsRemux);
//         if (ret < 0) {
//             logThrow(NULL, AV_LOG_ERROR,  "Could not open output file '%s'", urlOutput);
//             goto end;
//         }
//     }

//     // if (mp4Fragmented) {
//     //   av_dict_set(&pOptsRemux, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
//     // }

//     // rtsp tcp
//     if(av_dict_set(&pOptsRemux, "rtsp_transport", "tcp", 0) < 0) {
//         logThrow(NULL, AV_LOG_ERROR, "failed set output pOptsRemux");
//         ret = AVERROR_UNKNOWN;
//         goto end;
//     }

//     ret = avformat_write_header(pAVFormatRemux, &pOptsRemux);
//     if (ret < 0) {
//         logThrow(NULL, AV_LOG_ERROR,  "Error occurred when opening output file\n");
//         goto end;
//     }
//     while (1) {
//         AVStream *in_stream, *out_stream;
//         ret = av_read_frame(pAVFormatInput, &packet);
//         if (ret < 0)
//             break;
//         in_stream  = pAVFormatInput->streams[packet.stream_index];
//         if (packet.stream_index >= numStreams || streamList[packet.stream_index] < 0) {
//             av_packet_unref(&packet);
//             continue;
//         }
//         packet.stream_index = streamList[packet.stream_index];
//         out_stream = pAVFormatRemux->streams[packet.stream_index];
//         /* copy packet */
//         packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
//         packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
//         packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);
//         packet.pos = -1;

//         ret = av_interleaved_write_frame(pAVFormatRemux, &packet);
//         if (ret < 0) {
//             logThrow(NULL, AV_LOG_ERROR,  "Error muxing packet\n");
//             break;
//         }
//         av_packet_unref(&packet);
//     }

//     av_write_trailer(pAVFormatRemux);
// end:
//     avformat_close_input(&pAVFormatInput);
//     /* close output */
//     if (pAVFormatRemux && !(pAVFormatRemux->oformat->flags & AVFMT_NOFILE))
//         avio_closep(&pAVFormatRemux->pb);
//     avformat_free_context(pAVFormatRemux);
//     av_freep(&streamList);
//     if (ret < 0 && ret != AVERROR_EOF) {
//         logThrow(NULL, AV_LOG_ERROR,  "Error occurred: %s\n", av_err2str(ret));
//         return 1;
//     }
//     return 0;
}

