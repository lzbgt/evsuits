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

#include "vendor/include/zmq.h"
#include "tinythread.hpp"
#include "common.hpp"
#include "database.h"
#include "spdlog/spdlog.h"
#define MAX_ZMQ_MSG_SIZE 1204 * 1024 * 2

using namespace std;

class PacketPusher: public TinyThread {
private:
    void *pSubContext = NULL; // for packets relay
    void *pSubscriber = NULL;
    char *urlOut = NULL;
    char *urlIn = NULL;
    bool enablePush = false;
    int *streamList = NULL;
    AVFormatContext *pAVFormatRemux = NULL;
    AVFormatContext *pAVFormatInput = NULL;

    int getEnv(){
        // TODO:
        //urlOut = getenv("URL_OUT");
        //urlIn=
        urlOut = (char*)"rtsp://40.73.41.176:554/test1";
        return 0;
    }
    int setupMq()
    {
        teardownMq();
        int ret = 0;
        pSubContext = zmq_ctx_new();
        pSubscriber = zmq_socket(pSubContext, ZMQ_SUB);
        ret = zmq_setsockopt(pSubscriber, ZMQ_SUBSCRIBE, "", 0);
        if(ret != 0) {
            avlogThrow(NULL, AV_LOG_FATAL, "failed connect to pub");
        }
        ret = zmq_connect(pSubscriber, "tcp://localhost:5556");
        if(ret != 0) {
            avlogThrow(NULL, AV_LOG_FATAL, "failed create sub");
        }
        
        return 0;
    }

    int teardownMq()
    {
        if(pSubscriber != NULL) {
            zmq_close(pSubscriber);
        }
        if(pSubscriber != NULL) {
            zmq_ctx_destroy(pSubscriber);
        }
        return 0;
    }

    int setupStream(){
        int ret = 0, numStreams = 0, streamIdx = 0;
        AVDictionary *pOptsRemux = NULL, *pOptsInput = NULL, *pOptsOutput = NULL;
        urlIn = (char*)"rtsp://admin:FWBWTU@172.31.0.51/h264/ch1/sub/av_stream";
        if ((ret = avformat_open_input(&pAVFormatInput, urlIn, NULL, NULL)) < 0) {
            avlogThrow(NULL, AV_LOG_FATAL,  "Could not open input file '%s'", urlIn);
        }
        if ((ret = avformat_find_stream_info(pAVFormatInput, NULL)) < 0) {
            avlogThrow(NULL, AV_LOG_FATAL,  "Failed to retrieve input stream information");
        }

        //avformat_close_input(&pAVFormatInput);

        pAVFormatInput->flags = AVFMT_FLAG_NOBUFFER | AVFMT_FLAG_FLUSH_PACKETS;

        ret = avformat_alloc_output_context2(&pAVFormatRemux, NULL, "rtsp", urlOut);
        if (ret < 0) {
            avlogThrow(NULL, AV_LOG_FATAL, "failed create avformatcontext for output: %s", av_err2str(ret));
        }

        numStreams = pAVFormatInput->nb_streams;
        streamList = (int *)av_mallocz_array(numStreams, sizeof(*streamList));

        if (!streamList) {
            ret = AVERROR(ENOMEM);
            avlogThrow(NULL, AV_LOG_FATAL, "failed create avformatcontext for output: %s", av_err2str(AVERROR(ENOMEM)));
        }

        // find all video & audio streams for remuxing
        for (int i = 0; i < pAVFormatInput->nb_streams; i++) {
            AVStream *out_stream;
            AVStream *in_stream = pAVFormatInput->streams[i];
            AVCodecParameters *in_codecpar = in_stream->codecpar;
            if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
                    in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
                streamList[i] = -1;
                continue;
            }
            streamList[i] = streamIdx++;
            out_stream = avformat_new_stream(pAVFormatRemux, NULL);
            if (!out_stream) {
                avlogThrow(NULL, AV_LOG_FATAL,  "Failed allocating output stream\n");
                ret = AVERROR_UNKNOWN;

            }
            ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
            if (ret < 0) {
                avlogThrow(NULL, AV_LOG_FATAL,  "Failed to copy codec parameters\n");
            }
        }

        for(int i = 0; i < pAVFormatInput->nb_streams; i++ ) {
            spdlog::info("streamList[{:d}]: {:d}", i, streamList[i]);
        }

        av_dump_format(pAVFormatRemux, 0, urlOut, 1);

        if (!(pAVFormatRemux->oformat->flags & AVFMT_NOFILE)) {
            avlogThrow(NULL, AV_LOG_FATAL,  "Failed allocating output stream\n");
            ret = avio_open2(&pAVFormatRemux->pb, urlOut, AVIO_FLAG_WRITE, NULL, &pOptsRemux);
            if (ret < 0) {
                avlogThrow(NULL, AV_LOG_FATAL,  "Could not open output file '%s'", urlOut);
            }
        }

        // rtsp tcp
        if(av_dict_set(&pOptsRemux, "rtsp_transport", "tcp", 0) < 0) {
            avlogThrow(NULL, AV_LOG_FATAL, "failed set output pOptsRemux");
            ret = AVERROR_UNKNOWN;
        }

        ret = avformat_write_header(pAVFormatRemux, &pOptsRemux);
        if (ret < 0) {
            avlogThrow(NULL, AV_LOG_FATAL,  "Error occurred when opening output file\n");
        }
        return ret;
    }
protected:
    void run()
    {
        int ret = 0;
        bool bStopSig = false;
        zmq_msg_t msg;
        AVPacket packet;
        uint64_t pktCnt = 0;
        while (true) {
            if(checkStop() == true) {
                bStopSig = true;
                break;
            }
            int ret =zmq_msg_init(&msg);
            if(ret != 0) {
                spdlog::error("failed to init zmq msg");
                continue;
            }
            // receive packet
            ret = zmq_recvmsg(pSubscriber, &msg, 0);
            if(ret < 0) {
                spdlog::error("failed to recv zmq msg: {}", zmq_strerror(ret));
                continue;
            }

            // decode
            pktCnt++;
            ret = AVPacketSerializer::decode((char*)zmq_msg_data(&msg), ret, &packet); {
                if (ret < 0) {
                    spdlog::error("packet decode failed: {:d}", ret);
                    continue;
                }
            }
            zmq_msg_close(&msg);

            spdlog::debug("packet stream indx: {:d}", packet.stream_index);
            // relay
            AVStream *in_stream =NULL, *out_stream = NULL;
            in_stream  = pAVFormatInput->streams[packet.stream_index];
            packet.stream_index = streamList[packet.stream_index];
            out_stream = pAVFormatRemux->streams[packet.stream_index];

            //calc pts
            {
                spdlog::debug("seq: {:lld}, pts: {:lld}, dts: {:lld}, dur: {:lld}, idx: {:d}", pktCnt, packet.pts, packet.dts, packet.duration, packet.stream_index);
                /* copy packet */
                packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);
                packet.pos = -1;
            }
            
            ret = av_interleaved_write_frame(pAVFormatRemux, &packet);
            av_packet_unref(&packet);
            if (ret < 0) {
                spdlog::error("error muxing packet");
            }
        }
        av_write_trailer(pAVFormatRemux);
        if(!bStopSig && ret < 0) {
            //TOOD: reconnect
            spdlog::error("TODO: failed, reconnecting");
        }else {
            spdlog::error("exit on command");
        }
    }

public:
    PacketPusher()
    {
        getEnv();
        setupMq();
        setupStream();
    }

    ~PacketPusher()
    {
        teardownMq();
    }
};

int main(int argc, char *argv[]){
    av_log_set_level(AV_LOG_INFO);
    PacketPusher pusher;
    pusher.join();
}