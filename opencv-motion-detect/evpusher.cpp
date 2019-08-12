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

#define MAX_ZMQ_MSG_SIZE 1204 * 1024 * 2

using namespace std;
using json = nlohmann::json;
using namespace moodycamel;

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
    }
    int setupMq()
    {
        teardownMq();
        int ret = 0;
        pSubContext = zmq_ctx_new();
        pSubscriber = zmq_socket(pSubContext, ZMQ_SUB);
        ret = zmq_setsockopt(pSubscriber, ZMQ_SUBSCRIBE, "", 0);
        if(ret != 0) {
            logThrow(NULL, AV_LOG_FATAL, "failed connect to pub");
        }
        ret = zmq_connect(pSubscriber, "tcp://localhost:5556");
        if(ret != 0) {
            logThrow(NULL, AV_LOG_FATAL, "failed create sub");
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
            logThrow(NULL, AV_LOG_FATAL,  "Could not open input file '%s'", urlIn);
        }
        if ((ret = avformat_find_stream_info(pAVFormatInput, NULL)) < 0) {
            logThrow(NULL, AV_LOG_FATAL,  "Failed to retrieve input stream information");
        }

        pAVFormatInput->flags = AVFMT_FLAG_NOBUFFER | AVFMT_FLAG_FLUSH_PACKETS;

        ret = avformat_alloc_output_context2(&pAVFormatRemux, NULL, "rtsp", urlOut);
        if (ret < 0) {
            logThrow(NULL, AV_LOG_FATAL, "failed create avformatcontext for output: %s", av_err2str(ret));
        }

        numStreams = pAVFormatInput->nb_streams;
        streamList = (int *)av_mallocz_array(numStreams, sizeof(*streamList));

        if (!streamList) {
            ret = AVERROR(ENOMEM);
            logThrow(NULL, AV_LOG_FATAL, "failed create avformatcontext for output: %s", av_err2str(AVERROR(ENOMEM)));
        }

        // find all video & audio streams for remuxing
        for (int i = 0; i < pAVFormatInput->nb_streams; i++) {
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

        av_dump_format(pAVFormatRemux, 0, urlOut, 1);

        if (!(pAVFormatRemux->oformat->flags & AVFMT_NOFILE)) {
            logThrow(NULL, AV_LOG_FATAL,  "Failed allocating output stream\n");
            ret = avio_open2(&pAVFormatRemux->pb, urlOut, AVIO_FLAG_WRITE, NULL, &pOptsRemux);
            if (ret < 0) {
                logThrow(NULL, AV_LOG_FATAL,  "Could not open output file '%s'", urlOut);
            }
        }

        // rtsp tcp
        if(av_dict_set(&pOptsRemux, "rtsp_transport", "tcp", 0) < 0) {
            logThrow(NULL, AV_LOG_FATAL, "failed set output pOptsRemux");
            ret = AVERROR_UNKNOWN;
        }

        ret = avformat_write_header(pAVFormatRemux, &pOptsRemux);
        if (ret < 0) {
            logThrow(NULL, AV_LOG_FATAL,  "Error occurred when opening output file\n");
        }
    }
protected:
    void run()
    {
        int ret = 0;
        bool bStopSig = false;
        zmq_msg_t msg;
        av_log_set_level(AV_LOG_DEBUG);
        int pktCnt = 0;
        while (true) {
            if(checkStop() == true) {
                bStopSig = true;
                break;
            }
            int ret =zmq_msg_init(&msg);
            if(ret != 0) {
                av_log(NULL, AV_LOG_ERROR, "failed to init zmq msg");
                continue;
            }
            // receive packet
            ret = zmq_recvmsg(pSubscriber, &msg, 0);
            if(ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "failed to recv zmq msg");
                continue;
            }

            av_log(NULL, AV_LOG_DEBUG, "msg size: %d, %d\n", ret, zmq_msg_size(&msg));
            // deserialize the packet
            pktCnt++;
            AVPacket packet;
            av_log(NULL, AV_LOG_WARNING, "chkpt1: %d\n", pktCnt);
            ret = PacketSerializer::decode((char*)zmq_msg_data(&msg), ret, &packet); {
                if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR, "packet decode failed.");
                    continue;
                }
            }
            av_log(NULL, AV_LOG_WARNING, "chkpt2: %d\n", pktCnt);
            zmq_msg_close(&msg);
            // relay
            AVStream *in_stream =NULL, *out_stream = NULL;
            in_stream  = pAVFormatInput->streams[packet.stream_index];
            packet.stream_index = streamList[packet.stream_index];
            out_stream = pAVFormatRemux->streams[packet.stream_index];
            /* copy packet */
            packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);
            packet.pos = -1;
            
            av_log(NULL, AV_LOG_WARNING, "chkpt4: %d\n", pktCnt);
            ret = av_interleaved_write_frame(pAVFormatRemux, &packet);
            av_log(NULL, AV_LOG_WARNING, "chkpt5: %d\n", pktCnt);
            av_packet_unref(&packet);
            av_log(NULL, AV_LOG_WARNING, "chkpt6: %d\n", pktCnt);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR,  "Error muxing packet\n");
                continue;
            }
        }
        av_write_trailer(pAVFormatRemux);
        if(!bStopSig && ret < 0) {
            //TOOD: reconnect
            av_log(NULL, AV_LOG_ERROR, "TODO: failed, reconnecting");
        }else {
            av_log(NULL, AV_LOG_INFO, "exit on command");
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
    
    PacketPusher pusher;
    pusher.join();
}