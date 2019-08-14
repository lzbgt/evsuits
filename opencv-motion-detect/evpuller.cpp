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

using namespace std;

class EvPuller: public TinyThread {
private:
    void *pPubCtx = NULL; // for packets publishing
    void *pPub = NULL;
    void *pRepCtx = NULL; // for packets REP
    void *pRep = NULL;
    AVFormatContext *pAVFormatInput = NULL;
    string urlIn, urlPub;
    int *streamList = NULL, numStreams = 0;

public:
    EvPuller()
    {
        int ret = 0;
        do {
            init();
            ret =  setupMq();
        }while(ret < 0);
    }

    ~EvPuller()
    {
    }

protected:
    // Function to be executed by thread function
    void run()
    {
        int ret = 0;
        if ((ret = avformat_open_input(&pAVFormatInput, urlIn.c_str(), NULL, NULL)) < 0) {
            spdlog::error("Could not open input file {}", urlIn);
        }
        if ((ret = avformat_find_stream_info(pAVFormatInput, NULL)) < 0) {
            spdlog::error("Failed to retrieve input stream information");
        }

        pAVFormatInput->flags = AVFMT_FLAG_NOBUFFER | AVFMT_FLAG_FLUSH_PACKETS;

        numStreams = pAVFormatInput->nb_streams;
        int *streamList = (int *)av_mallocz_array(numStreams, sizeof(*streamList));

        if (!streamList) {
            ret = AVERROR(ENOMEM);
            spdlog::error("failed create avformatcontext for output: {}", av_err2str(AVERROR(ENOMEM)));
        }

        // find all video & audio streams for remuxing
        int i = 0, streamIdx = 0;
        for (; i < pAVFormatInput->nb_streams; i++) {
            AVStream *in_stream = pAVFormatInput->streams[i];
            AVCodecParameters *in_codecpar = in_stream->codecpar;
            if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
                    in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
                    in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
                streamList[i] = -1;
                continue;
            }
            streamList[i] = streamIdx++;
        }

        bool bStopSig = false;
        int pktCnt = 0;
        while (true) {
            if(checkStop() == true) {
                bStopSig = true;
                break;
            }
            AVStream *in_stream;
            AVPacket packet;
            zmq_msg_t msg;

            ret = av_read_frame(pAVFormatInput, &packet);
            if (ret < 0) {
                spdlog::error("failed read packet: {}", av_err2str(ret));
                break;
            }
            in_stream  = pAVFormatInput->streams[packet.stream_index];
            if (packet.stream_index >= numStreams || streamList[packet.stream_index] < 0) {
                av_packet_unref(&packet);
                continue;
            }

            pktCnt++;

            packet.stream_index = streamList[packet.stream_index];

            /* copy packet */
            //packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            //packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            //packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);
            //packet.pos = -1;

            // serialize packet to raw bytes
            char * data = NULL;
            int size = AVPacketSerializer::encode(packet, &data);
            zmq_msg_init_data(&msg, (void*)data, size, mqPacketFree, NULL);
            zmq_send_const(pPub, zmq_msg_data(&msg), size, 0);

            av_packet_unref(&packet);
        }

        // TODO:
        if(ret < 0 && !bStopSig) {
            // reconnect
        }
        else {
            std::cout << "Task End" << std::endl;
        }
    }

private:
    int init()
    {
        bool inited = false;

        while(!inited) {
            // TODO: read db to get sn
            const char* sn = "ILS-2";
            // req config
            json jr = cloudutils::registry(sn, "evpuller", 0);
            bool bcnt = false;
            try {
                spdlog::info("registry: {:s}", jr.dump());
                string ipc = jr["data"]["ipc"];
                string user = jr["data"]["username"];
                string passwd = jr["data"]["password"];
                json data = jr["data"]["services"]["evpuller"];
                urlIn = "rtsp://" + user + ":" + passwd + "@"+ ipc + "/h264/ch1/sub/av_stream";
                urlPub = string("tcp://") +data["addr"].get<string>() + ":" + to_string(data["port-pub"]);
            }
            catch(exception &e) {
                bcnt = true;
                spdlog::error(e.what());
            }
            if(bcnt) {
                this_thread::sleep_for(chrono::milliseconds(1000*10));
                continue;
            }

            inited = true;
        }

        return 0;
    }

    int setupMq()
    {
        teardownMq();
        pPubCtx = zmq_ctx_new();
        pPub = zmq_socket(pPubCtx, ZMQ_PUB);

        int rc = zmq_bind(pPub, urlPub.c_str());
        if(rc != 0) {
            spdlog::error("failed create pub: {}, {}", zmq_strerror(rc), urlPub.c_str());
            this_thread::sleep_for(chrono::milliseconds(100*10));
            return -1;
        }

        return 0;
    }

    int teardownMq()
    {
        if(pPub != NULL) {
            zmq_close(pPub);
        }
        if(pPubCtx != NULL) {
            zmq_ctx_destroy(pPubCtx);
        }
        return 0;
    }
};



int main(int argc, char **argv)
{
    av_log_set_level(AV_LOG_INFO);
    spdlog::set_level(spdlog::level::info);
    DB::exec(NULL, NULL, NULL,NULL);
    auto evp = EvPuller();
    evp.join();
    return 0;
}

