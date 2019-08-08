#pragma GCC diagnostic ignored "-Wunused-private-field"
#pragma GCC diagnostic ignored "-Wunused-variable"


#include <stdlib.h>
#include <string>
#include <thread>
#include <iostream>
#include <chrono>
#include <future>

#ifdef OS_LINUX
#include <filesystem>
namespace fs = std::filesystem;
#endif

#include  "vendor/include/zmq.h"
#include "inc/json.hpp"
#include "inc/blockingconcurrentqueue.hpp"
#include "inc/tinythread.hpp"
#include "inc/common.hpp"

using namespace std;
using json = nlohmann::json;
using namespace moodycamel;

class PacketPusher: public TinyThread {
private:
    void *pPubContext = NULL; // for packets publishing
    void *pPublisher = NULL;
    AVFormatContext *pAVFormatInput = NULL, *pAVFormatRemux = NULL; 
    string urlIn;
    int *streamList = NULL, numStreams = 0;
    
public:
  PacketPusher(string urlIn):urlIn(urlIn){
    
    setupMq();
  }

  ~PacketPusher(){
  }

protected:
    // Function to be executed by thread function
    void run()
    {
        int ret = 0;
        if ((ret = avformat_open_input(&pAVFormatInput, urlIn.c_str(), NULL, NULL)) < 0) {
            logThrow(NULL, AV_LOG_FATAL,  "Could not open input file '%s'", urlIn.c_str());
        }
        if ((ret = avformat_find_stream_info(pAVFormatInput, NULL)) < 0) {
            logThrow(NULL, AV_LOG_FATAL,  "Failed to retrieve input stream information");
        }

        pAVFormatInput->flags = AVFMT_FLAG_NOBUFFER | AVFMT_FLAG_FLUSH_PACKETS;

        numStreams = pAVFormatInput->nb_streams;
        int *streamList = (int *)av_mallocz_array(numStreams, sizeof(*streamList));

        if (!streamList) {
            ret = AVERROR(ENOMEM);
            logThrow(NULL, AV_LOG_FATAL, "failed create avformatcontext for output: %s", av_err2str(AVERROR(ENOMEM)));
        }

        // find all video & audio streams for remuxing
        int i = 0, streamIdx = 0;
        for (; i < pAVFormatInput->nb_streams; i++) {
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
                logThrow(NULL, AV_LOG_FATAL,  "Failed allocating output stream\n");
                ret = AVERROR_UNKNOWN;

            }
            ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
            if (ret < 0) {
                logThrow(NULL, AV_LOG_FATAL,  "Failed to copy codec parameters\n");
            }
        }

        while (checkStop() == false) {
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
                logThrow(NULL, AV_LOG_FATAL,  "Error muxing packet\n");
                break;
            }
            av_packet_unref(&packet);
        }

        av_write_trailer(pAVFormatRemux);
        std::cout << "Pusher Start" << std::endl;
        // Check if thread is requested to stop ?
        while (checkStop() == false) {
            std::cout << "Doing Some Work" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        }
        std::cout << "Task End" << std::endl;
    }

    int setupMq()
    {
        teardownMq();
        pPubContext = zmq_ctx_new();
        pPublisher = zmq_socket(pPubContext, ZMQ_PUB);

        int rc = zmq_bind(pPublisher, "tcp://*:5556");
        if(rc != 0) {
            logThrow(NULL, AV_LOG_FATAL, "failed create pub");
        }

        return 0;
    }

    int teardownMq()
    {
        if(pPublisher != NULL) {
            zmq_close(pPublisher);
        }
        if(pPubContext != NULL) {
            zmq_ctx_destroy(pPubContext);
        }
        return 0;
    }

};



class VideoProcessor {
private:
#define SECS_SLICE (60*5/2)
    AVFormatContext *pAVFormatInput = NULL, *pAVFormatRemux = NULL;
    AVCodec *pCodec = NULL;
    AVDictionary *pOptsRemux = NULL, *pOptsInput = NULL, *pOptsOutput = NULL;
    int idxVideo = -1, idxAudio = -1, numStreams = 0, numSlices = 6, secsSlice = SECS_SLICE;
    int *streamList = NULL;
    bool bPush = true, bRecord = false;
    string urlIn, urlOut, pathSlice;
    unordered_map<string, string> envParams = unordered_map<string, string>();
    // mq
    void *pRepContext = NULL; // for msg from edge gateway
    void *pReqContext = NULL; // for msg to edge gateway
private:
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
                logThrow(NULL, AV_LOG_FATAL, "can't create directory: %s", pathSlice.c_str());
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
            logThrow(NULL, AV_LOG_FATAL, "no input/output url");
            exit(1);
        }
    }

    int setupStreams()
    {
        PacketPusher pusher(urlIn);
        pusher.detach();
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        pusher.stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(9993000));
        int ret = 0, i, streamIdx = 0;
        // if ((ret = avformat_open_input(&pAVFormatInput, urlIn.c_str(), NULL, NULL)) < 0) {
        //     logThrow(NULL, AV_LOG_FATAL,  "Could not open input file '%s'", urlIn.c_str());
        // }
        // if ((ret = avformat_find_stream_info(pAVFormatInput, NULL)) < 0) {
        //     logThrow(NULL, AV_LOG_FATAL,  "Failed to retrieve input stream information");
        // }

        // pAVFormatInput->flags = AVFMT_FLAG_NOBUFFER | AVFMT_FLAG_FLUSH_PACKETS;

        // ret = avformat_alloc_output_context2(&pAVFormatRemux, NULL, "rtsp", urlOut.c_str());
        // if (ret < 0) {
        //     logThrow(NULL, AV_LOG_FATAL, "failed create avformatcontext for output: %s", av_err2str(ret));
        // }

        // numStreams = pAVFormatInput->nb_streams;
        // streamList = (int *)av_mallocz_array(numStreams, sizeof(*streamList));

        // if (!streamList) {
        //     ret = AVERROR(ENOMEM);
        //     logThrow(NULL, AV_LOG_FATAL, "failed create avformatcontext for output: %s", av_err2str(AVERROR(ENOMEM)));
        // }

        // // find all video & audio streams for remuxing
        // for (i = 0; i < pAVFormatInput->nb_streams; i++) {
        //     AVStream *out_stream;
        //     AVStream *in_stream = pAVFormatInput->streams[i];
        //     AVCodecParameters *in_codecpar = in_stream->codecpar;
        //     if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
        //             in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
        //             in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
        //         streamList[i] = -1;
        //         continue;
        //     }
        //     streamList[i] = streamIdx++;
        //     out_stream = avformat_new_stream(pAVFormatRemux, NULL);
        //     if (!out_stream) {
        //         logThrow(NULL, AV_LOG_FATAL,  "Failed allocating output stream\n");
        //         ret = AVERROR_UNKNOWN;

        //     }
        //     ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        //     if (ret < 0) {
        //         logThrow(NULL, AV_LOG_FATAL,  "Failed to copy codec parameters\n");
        //     }
        // }

        // av_dump_format(pAVFormatRemux, 0, urlOut.c_str(), 1);

        // // find best video stream
        // idxVideo = av_find_best_stream(pAVFormatInput, AVMEDIA_TYPE_VIDEO, -1, -1, &pCodec, 0);
        // if(idxVideo < 0) {
        //     logThrow(NULL, AV_LOG_FATAL, "failed find best video stream");
        // }

        // if (!(pAVFormatRemux->oformat->flags & AVFMT_NOFILE)) {
        //     logThrow(NULL, AV_LOG_FATAL,  "Failed allocating output stream\n");
        //     ret = avio_open2(&pAVFormatRemux->pb, urlOut.c_str(), AVIO_FLAG_WRITE, NULL, &pOptsRemux);
        //     if (ret < 0) {
        //         logThrow(NULL, AV_LOG_FATAL,  "Could not open output file '%s'", urlOut.c_str());
        //     }
        // }

        // // rtsp tcp
        // if(av_dict_set(&pOptsRemux, "rtsp_transport", "tcp", 0) < 0) {
        //     logThrow(NULL, AV_LOG_FATAL, "failed set output pOptsRemux");
        //     ret = AVERROR_UNKNOWN;
        // }

        // ret = avformat_write_header(pAVFormatRemux, &pOptsRemux);
        // if (ret < 0) {
        //     logThrow(NULL, AV_LOG_FATAL,  "Error occurred when opening output file\n");
        // }
        // while (1) {
        //     AVStream *in_stream, *out_stream;
        //     AVPacket packet;
        //     ret = av_read_frame(pAVFormatInput, &packet);
        //     if (ret < 0)
        //         break;
        //     in_stream  = pAVFormatInput->streams[packet.stream_index];
        //     if (packet.stream_index >= numStreams || streamList[packet.stream_index] < 0) {
        //         av_packet_unref(&packet);
        //         continue;
        //     }
        //     packet.stream_index = streamList[packet.stream_index];
        //     out_stream = pAVFormatRemux->streams[packet.stream_index];
        //     /* copy packet */
        //     packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        //     packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        //     packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);
        //     packet.pos = -1;

        //     ret = av_interleaved_write_frame(pAVFormatRemux, &packet);
        //     if (ret < 0) {
        //         logThrow(NULL, AV_LOG_FATAL,  "Error muxing packet\n");
        //         break;
        //     }
        //     av_packet_unref(&packet);
        // }

        // av_write_trailer(pAVFormatRemux);
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
    ~VideoProcessor()
    {
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
//         logThrow(NULL, AV_LOG_FATAL,  "Could not open input file '%s'", urlInput);
//         goto end;
//     }
//     if ((ret = avformat_find_stream_info(pAVFormatInput, NULL)) < 0) {
//         logThrow(NULL, AV_LOG_FATAL,  "Failed to retrieve input stream information");
//         goto end;
//     }

//     ret = avformat_alloc_output_context2(&pAVFormatRemux, NULL, "rtsp", urlOutput);
//     if (ret < 0) {
//         logThrow(NULL, AV_LOG_FATAL, "failed create avformatcontext for output: %s", av_err2str(ret));
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
//             logThrow(NULL, AV_LOG_FATAL,  "Failed allocating output stream\n");
//             ret = AVERROR_UNKNOWN;
//             goto end;
//         }
//         ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
//         if (ret < 0) {
//             logThrow(NULL, AV_LOG_FATAL,  "Failed to copy codec parameters\n");
//             goto end;
//         }
//     }

//     av_dump_format(pAVFormatRemux, 0, urlOutput, 1);

//     // find best video stream
//     idxVideo = av_find_best_stream(pAVFormatInput, AVMEDIA_TYPE_VIDEO, -1, -1, &pCodec, 0);
//     if(idxVideo < 0) {
//         logThrow(NULL, AV_LOG_FATAL, "failed find best video stream");
//         goto end;
//     }


//     // unless it's a no file (we'll talk later about that) write to the disk (FLAG_WRITE)
//     // but basically it's a way to save the file to a buffer so you can store it
//     // wherever you want.

//     if (!(pAVFormatRemux->oformat->flags & AVFMT_NOFILE)) {
//         logThrow(NULL, AV_LOG_FATAL,  "Failed allocating output stream\n");
//         ret = avio_open2(&pAVFormatRemux->pb, urlOutput, AVIO_FLAG_WRITE, NULL, &pOptsRemux);
//         if (ret < 0) {
//             logThrow(NULL, AV_LOG_FATAL,  "Could not open output file '%s'", urlOutput);
//             goto end;
//         }
//     }

//     // if (mp4Fragmented) {
//     //   av_dict_set(&pOptsRemux, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
//     // }

//     // rtsp tcp
//     if(av_dict_set(&pOptsRemux, "rtsp_transport", "tcp", 0) < 0) {
//         logThrow(NULL, AV_LOG_FATAL, "failed set output pOptsRemux");
//         ret = AVERROR_UNKNOWN;
//         goto end;
//     }

//     ret = avformat_write_header(pAVFormatRemux, &pOptsRemux);
//     if (ret < 0) {
//         logThrow(NULL, AV_LOG_FATAL,  "Error occurred when opening output file\n");
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
//             logThrow(NULL, AV_LOG_FATAL,  "Error muxing packet\n");
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
//         logThrow(NULL, AV_LOG_FATAL,  "Error occurred: %s\n", av_err2str(ret));
//         return 1;
//     }
//     return 0;
}

