#pragma GCC diagnostic ignored "-Wunused-private-field"
#pragma GCC diagnostic ignored "-Wunused-variable"

#include <stdlib.h>
#include <string>
#include <thread>
#include <iostream>
#include <chrono>
#include <future>
#include <vector>

#ifdef OS_LINUX
#include <filesystem>
namespace fs = std::filesystem;
#endif
#include <cstdlib>
#include <opencv2/opencv.hpp>
#include "vendor/include/zmq.h"
#include "tinythread.hpp"
#include "common.hpp"
#include "database.h"

using namespace std;

class EvMLMotion: public TinyThread {
private:
#define URLOUT_DEFAULT "slices"
#define NUM_DAYS_DEFAULT 2
#define MINUTES_PER_SLICE_DEFAULT 1
// 2 days, 10 minutes per record
#define NUM_SLICES_DEFAULT (24 * NUM_DAYS_DEFAULT * 60 / MINUTES_PER_SLICE_DEFAULT)
    void *pSubCtx = NULL, *pReqCtx = NULL; // for packets relay
    void *pSub = NULL, *pReq = NULL;
    string urlOut, urlPub, urlRep, sn;
    int iid, days, minutes, numSlices, lastSliceId;
    bool enablePush = false;
    AVFormatContext *pAVFormatRemux = NULL;
    AVFormatContext *pAVFormatInput = NULL;
    AVDictionary *pOptsRemux = NULL;
    // load from db
    vector<int> *sliceIdxToName = NULL;
    int *streamList = NULL;
    int streamIdx = -1;

    int init()
    {
        int ret = 0;
        bool inited = false;
        // TODO: read db to get sn
        sn = "ILS-3";
        iid = 3;
        while(!inited) {
            // req config
            json jr = cloudutils::registry(sn.c_str(), "evmlmotion", iid);
            bool bcnt = false;
            try {
                spdlog::info("registry: {:s}", jr.dump());
                json data = jr["data"]["services"]["evpuller"];
                string addr = data["addr"].get<string>();
                if(addr == "0.0.0.0") {
                    addr = "localhost";
                }
                urlPub = string("tcp://") + addr + ":" + to_string(data["port-pub"]);
                urlRep = string("tcp://") + addr + ":" + to_string(data["port-rep"]);
                spdlog::info("evmlmotion {} {} will connect to {} for sub, {} for req", sn, iid, urlPub, urlRep);

                data = jr["data"]["services"]["evmlmotion"];
                for(auto &j: data) {
                    if(j["sn"] == sn && iid == j["iid"] && j["enabled"] != 0) {
                        try{
                            j.at("path").get_to(urlOut);
                        }catch(exception &e) {
                            spdlog::warn("evmlmotion {} {} exception get params for storing slices: {}, using default: {}", sn, iid, e.what(), URLOUT_DEFAULT);
                            urlOut = URLOUT_DEFAULT;
                        }
                        try{
                            j.at("days").get_to(days);
                        }catch(exception &e) {
                            spdlog::warn("evmlmotion {} {} exception get params for storing slices: {}, using default: {}", sn, iid, e.what(), NUM_DAYS_DEFAULT);
                            days = NUM_DAYS_DEFAULT;
                        }
                        try{
                            j.at("minutes").get_to(minutes);
                        }catch(exception &e) {
                            spdlog::warn("evmlmotion {} {} exception get params for storing slices: {}, using default: {}", sn, iid, e.what(),MINUTES_PER_SLICE_DEFAULT);
                            minutes = MINUTES_PER_SLICE_DEFAULT;
                        }

                        numSlices = 24 * days * 60 /minutes;
                        // alloc memory
                        sliceIdxToName = new vector<int>(numSlices);
                        // load db
                        // DB::exec(NULL, "select id, ts, last from slices;", DB::get_slices, sliceIdxToName);
                        spdlog::info("mkdir -p {}", urlOut);
                        ret = system((string("mkdir -p ") + urlOut).c_str());
                        if(ret == -1) {
                            spdlog::error("failed to create {} dir", urlOut);
                            return -1;
                        }

                        break;
                    }
                }
            }
            catch(exception &e) {
                bcnt = true;
                spdlog::error("evmlmotion {} {} exception in EvPuller.init {:s},  retrying...", sn, iid, e.what());
            }
            if(bcnt || urlOut.empty()) {
                // TODO: waiting for command
                spdlog::warn("evmlmotion {} {} waiting for command & retrying", sn, iid);
                this_thread::sleep_for(chrono::milliseconds(1000*20));
                continue;
            }

            inited = true;
        }

        return 0;
    }
    int setupMq()
    {
        teardownMq();
        int ret = 0;

        // setup sub
        pSubCtx = zmq_ctx_new();
        pSub = zmq_socket(pSubCtx, ZMQ_SUB);
        ret = zmq_setsockopt(pSub, ZMQ_SUBSCRIBE, "", 0);
        if(ret != 0) {
            spdlog::error("evmlmotion failed connect to pub: {}, {}", sn, iid);
            return -1;
        }
        ret = zmq_connect(pSub, urlPub.c_str());
        if(ret != 0) {
            spdlog::error("evmlmotion {} {} failed create sub", sn, iid);
            return -2;
        }

        // setup req
        pReqCtx = zmq_ctx_new();
        pReq = zmq_socket(pReqCtx, ZMQ_REQ);
        spdlog::info("evmlmotion {} {} try create req to {}", sn, iid, urlRep);
        ret = zmq_connect(pReq, urlRep.c_str());

        if(ret != 0) {
            spdlog::error("evmlmotion {} {} failed create req to {}", sn, iid, urlRep);
            return -3;
        }

        spdlog::info("evmlmotion {} {} success setupMq", sn, iid);

        return 0;
    }

    int teardownMq()
    {
        if(pSub != NULL) {
            zmq_close(pSub);
            pSub = NULL;
        }
        if(pSubCtx != NULL) {
            zmq_ctx_destroy(pSubCtx);
            pSubCtx = NULL;
        }
        if(pReq != NULL) {
            zmq_close(pSub);
            pReq = NULL;
        }
        if(pReqCtx != NULL) {
            zmq_ctx_destroy(pSub);
            pReqCtx = NULL;
        }

        return 0;
    }

    int setupStream()
    {
        int ret = 0;

        // req avformatcontext packet
        // send first packet to init connection
        zmq_msg_t msg;
        zmq_send(pReq, "hello", 5, 0);
        spdlog::info("evmlmotion {} {} success send hello", sn, iid);
        ret =zmq_msg_init(&msg);
        if(ret != 0) {
            spdlog::error("failed to init zmq msg");
            exit(1);
        }
        // receive packet
        ret = zmq_recvmsg(pReq, &msg, 0);
        spdlog::info("evmlmotion {} {} recv", sn, iid);
        if(ret < 0) {
            spdlog::error("evmlmotion {} {} failed to recv zmq msg: {}", sn, iid, zmq_strerror(ret));
            exit(1);
        }

        pAVFormatInput = (AVFormatContext *)malloc(sizeof(AVFormatContext));
        AVFormatCtxSerializer::decode((char *)zmq_msg_data(&msg), ret, pAVFormatInput);

        // close req
        {
            zmq_msg_close(&msg);
            if(pReq != NULL) {
                zmq_close(pReq);
                pReq = NULL;
            }
            if(pReqCtx != NULL) {
                zmq_ctx_destroy(pReqCtx);
                pReqCtx = NULL;
            }
        }

        // ret = avformat_alloc_output_context2(&pAVFormatRemux, NULL, "mpg", urlOut.c_str());
        // if (ret < 0) {
        //     spdlog::error("evmlmotion {} {} failed create avformatcontext for output: %s", sn, iid, av_err2str(ret));
        //     exit(1);
        // }

        //spdlog::info("evmlmotion {} {} numStreams: {:d}", sn, iid, pAVFormatInput->nb_streams);

        // find all video & audio streams for remuxing
        streamList = (int *)av_mallocz_array(pAVFormatInput->nb_streams, sizeof(*streamList));
        for (int i = 0; i < pAVFormatInput->nb_streams; i++) {
            AVStream *out_stream;
            AVStream *in_stream = pAVFormatInput->streams[i];
            AVCodecParameters *in_codecpar = in_stream->codecpar;
            if (in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
                streamList[i] = -1;
                continue;
            }
            streamList[i] = streamIdx++;
            break;
        }

        for(int i = 0; i < pAVFormatInput->nb_streams; i++ ) {
            spdlog::info("streamList[{:d}]: {:d}", i, streamList[i]);
        }

        //av_dict_set(&pOptsRemux, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
        return ret;
    }
protected:
    void run()
    {
        bool bStopSig = false;
        int ret = 0;
        int idx = 0;
        int pktCnt = 0;
        AVStream * out_stream = NULL;
        zmq_msg_t msg;
        AVPacket packet, keyPacket;
        av_init_packet(&keyPacket);
        while(true) {
            if(checkStop() == true) {
                bStopSig = true;
                break;
            }
            // business logic
            int ret =zmq_msg_init(&msg);
            ret = zmq_recvmsg(pSub, &msg, 0);
            if(ret < 0) {
                spdlog::error("failed to recv zmq msg: {}", zmq_strerror(ret));
                continue;
            }
            ret = AVPacketSerializer::decode((char*)zmq_msg_data(&msg), ret, &packet);
            {
                if (ret < 0) {
                    spdlog::error("packet decode failed: {:d}", ret);
                    continue;
                }
            }

            zmq_msg_close(&msg);

            AVStream *in_stream =NULL, *out_stream = NULL;
            in_stream  = pAVFormatInput->streams[packet.stream_index];
            packet.stream_index = streamList[packet.stream_index];
            out_stream = pAVFormatRemux->streams[packet.stream_index];
            //calc pts
            {
                if(pktCnt % 1024 == 0) {
                    spdlog::info("seq: {}, pts: {}, dts: {}, dur: {}, idx: {}", pktCnt, packet.pts, packet.dts, packet.duration, packet.stream_index);
                }
                pktCnt++;
                
                packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);
                packet.pos = -1;
            }
            // TODO:
            if(packet.data[5] == 0x65 ) {
                spdlog::info("pktCnt: {}, keyframe: {:0x}", pktCnt, packet.data[5]);
                if(keyPacket.buf != NULL) {
                    av_packet_unref(&keyPacket);
                    av_packet_ref(&keyPacket, &packet);
                }
            }

            ret = av_interleaved_write_frame(pAVFormatRemux, &packet);
            av_packet_unref(&packet);
            if (ret < 0) {
                spdlog::error("error muxing packet");
            }
        }// while in slice
        // write tail
        av_write_trailer(pAVFormatRemux);
        // close output context
        if (pAVFormatRemux && !(pAVFormatRemux->oformat->flags & AVFMT_NOFILE))
            avio_closep(&pAVFormatRemux->pb);
        avformat_free_context(pAVFormatRemux);
    
    }
public:
    EvMLMotion() {
        init();
        setupMq();
        setupStream();
    };
    ~EvMLMotion() {};
};

int main(int argc, const char *argv[])
{
    spdlog::set_level(spdlog::level::info);
    EvMLMotion es;
    es.join();
    return 0;
}