/*
module: evpusher
description:
author: Bruce.Lu <lzbgt@icloud.com>
update: 2019/08/23
*/

#pragma GCC diagnostic ignored "-Wunused-private-field"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"

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

#include "zmqhelper.hpp"
#include "tinythread.hpp"
#include "common.hpp"
#include "database.h"
#include "spdlog/spdlog.h"
#define MAX_ZMQ_MSG_SIZE 1204 * 1024 * 2

using namespace std;
using namespace zmqhelper;

class EvPusher: public TinyThread {
private:
    void *pSubCtx = NULL, *pDealerCtx = NULL; // for packets relay
    void *pSub = NULL, *pDealer = NULL;
    string urlOut, urlPub, urlDealer, devSn, pullerGid, mgrSn, selfId;
    int iid;
    bool enablePush = false;
    int *streamList = NULL;
    AVFormatContext *pAVFormatRemux = NULL;
    AVFormatContext *pAVFormatInput = NULL;
    json config;
    thread thPing;

    int init()
    {
        bool inited = false;
        // TODO: read db to get devSn
        devSn = "ILSEVPUSHER1";
        iid = 1;
        selfId = devSn + ":evpusher:" + to_string(iid);
        while(!inited) {
            // TODO: req config
            bool found = false;
            try {
                config = json::parse(cloudutils::config);
                spdlog::info("config: {:s}", config.dump());
                json evpusher;
                json evmgr;
                json ipc;

                json data = config["data"];
                for (auto& [key, value] : data.items()) {
                    //std::cout << key << " : " << dynamic_cast<json&>(value).dump() << "\n";
                    evmgr = value;
                    json ipcs = evmgr["ipcs"];
                    for(auto &j: ipcs) {
                        json pullers = j["modules"]["evpusher"];
                        for(auto &p:pullers) {
                            if(p["sn"] == devSn && p["iid"] == iid) {
                                evpusher = p;
                                break;
                            }
                        }
                        if(evpusher.size() != 0) {
                            ipc = j;
                            break;
                        }
                    }

                    if(ipc.size()!=0 && evpusher.size()!=0) {
                        found = true;
                        break;
                    }
                }

                if(!found) {
                    spdlog::error("evpusher {} {}: no valid config found. retrying load config...", devSn, iid);
                    this_thread::sleep_for(chrono::seconds(3));
                    continue;
                }

                // TODO: currently just take the first puller, but should test connectivity
                json evpuller = ipc["modules"]["evpuller"][0];
                pullerGid = evpuller["sn"].get<string>() + ":evpuller:" + to_string(evpuller["iid"]);
                mgrSn = evmgr["sn"];

                urlPub = string("tcp://") + evpuller["addr"].get<string>() + ":" + to_string(evpuller["port-pub"]);
                urlDealer = string("tcp://") + evmgr["addr"].get<string>() + ":" + to_string(evmgr["port-router"]);
                spdlog::info("evpusher {} {} will connect to {} for sub, {} for router", devSn, iid, urlPub, urlDealer);
                // TODO: multiple protocols support
                urlOut = evpusher["urlDest"].get<string>();
            }
            catch(exception &e) {
                spdlog::error("evpusher {} {} exception in EvPuller.init {:s} retrying", devSn, iid, e.what());
                this_thread::sleep_for(chrono::seconds(3));
                continue;
            }

            inited = true;
        }

        return 0;
    }

    int ping()
    {
        // send hello to router
        int ret = 0;
        vector<vector<uint8_t> >body;
        // since identity is auto set
        body.push_back(str2body(mgrSn+":0:0"));
        body.push_back(str2body(EV_MSG_META_PING)); // blank meta
        body.push_back(str2body(MSG_HELLO));

        ret = z_send_multiple(pDealer, body);
        if(ret < 0) {
            spdlog::error("evpusher {} {} failed to send multiple: {}", devSn, iid, zmq_strerror(zmq_errno()));
            //TODO:
        }
        else {
            spdlog::info("evpusher {} {} sent hello to router: {}", devSn, iid, mgrSn);
        }

        return ret;
    }

    int setupMq()
    {
        int ret = 0;

        // setup sub
        pSubCtx = zmq_ctx_new();
        pSub = zmq_socket(pSubCtx, ZMQ_SUB);
        ret = zmq_setsockopt(pSub, ZMQ_SUBSCRIBE, "", 0);
        if(ret != 0) {
            spdlog::error("evpusher {} {} failed set setsockopt: {}", devSn, iid, urlPub);
            return -1;
        }
        ret = zmq_connect(pSub, urlPub.c_str());
        if(ret != 0) {
            spdlog::error("evpusher {} {} failed connect pub: {}", devSn, iid, urlPub);
            return -2;
        }

        // setup dealer
        pDealerCtx = zmq_ctx_new();
        pDealer = zmq_socket(pDealerCtx, ZMQ_DEALER);
        spdlog::info("evpusher {} {} try create req to {}", devSn, iid, urlDealer);
        ret = zmq_setsockopt(pDealer, ZMQ_IDENTITY, selfId.c_str(), selfId.size());
        ret += zmq_setsockopt (pDealer, ZMQ_ROUTING_ID, selfId.c_str(), selfId.size());
        if(ret < 0) {
            spdlog::error("evpusher {} {} failed setsockopts router: {}", devSn, iid, urlDealer);
            return -3;
        }
        ret = zmq_connect(pDealer, urlDealer.c_str());
        if(ret != 0) {
            spdlog::error("evpusher {} {} failed connect dealer: {}", devSn, iid, urlDealer);
            return -4;
        }
        //ping
        ret = ping();
        // TODO: don't need this anymore, since I've used the draft feature of ZOUTER_NOTIFICATION instead
        // thPing = thread([&,this]() {
        //     while(true) {
        //         this_thread::sleep_for(chrono::seconds(EV_HEARTBEAT_SECONDS-2));
        //         ping();
        //     }
        // });

        // thPing.detach();

        return ret;
    }

    int getInputFormat()
    {
        int ret = 0;
        // req avformatcontext packet
        // send hello to puller
        spdlog::info("evpusher {} {} send hello to puller: {}", devSn, iid, pullerGid);
        vector<vector<uint8_t> > body;
        body.push_back(str2body(pullerGid));
        json meta;
        meta["type"] = EV_MSG_META_AVFORMATCTX;
        body.push_back(str2body(meta.dump()));
        body.push_back(str2body(MSG_HELLO));
        bool gotFormat = false;
        uint64_t failedCnt = 0;
        while(!gotFormat) {
            ret = z_send_multiple(pDealer, body);
            if(ret < 0) {
                spdlog::error("evpusher {} {}, failed to send hello to puller: {}", devSn, iid, zmq_strerror(zmq_errno()));
                continue;
            }

            // expect response with avformatctx
            auto v = z_recv_multiple(pDealer);
            if(v.size() != 3) {
                ret = zmq_errno();
                if(ret != 0) {
                    if(failedCnt % 100 == 0) {
                        spdlog::error("evpusher {} {}, error receive avformatctx: {}, {}", devSn, iid, v.size(), zmq_strerror(ret));
                        spdlog::info("evpusher {} {} retry connect to peers", devSn, iid);
                    }
                    this_thread::sleep_for(chrono::seconds(5));
                    failedCnt++;
                }
                else {
                    spdlog::error("evpusher {} {}, received bad size zmq msg for avformatctx: {}", devSn, iid, v.size());
                }
            }
            else if(body2str(v[0]) != pullerGid) {
                spdlog::error("evpusher {} {}, invalid sender for avformatctx: {}, should be: {}", devSn, iid, body2str(v[0]), pullerGid);
            }
            else {
                try {
                    auto cmd = json::parse(body2str(v[1]));
                    if(cmd["type"].get<string>() == EV_MSG_META_AVFORMATCTX) {
                        pAVFormatInput = (AVFormatContext *)malloc(sizeof(AVFormatContext));
                        AVFormatCtxSerializer::decode((char *)(v[2].data()), v[2].size(), pAVFormatInput);
                        gotFormat = true;
                    }
                }
                catch(exception &e) {
                    spdlog::error("evpusher {} {}, exception in parsing avformatctx packet: {}", devSn, iid, e.what());
                }
            }
        }
        return ret;
    }

    int setupStream()
    {
        int ret = 0;
        AVDictionary *pOptsRemux = NULL;

        ret = avformat_alloc_output_context2(&pAVFormatRemux, NULL, "rtsp", urlOut.c_str());
        if (ret < 0) {
            spdlog::error("evpusher {} {} failed create avformatcontext for output: %s", devSn, iid, av_err2str(ret));
            exit(1);
        }

        streamList = (int *)av_mallocz_array(pAVFormatInput->nb_streams, sizeof(*streamList));
        spdlog::info("evpusher {} {} numStreams: {:d}", devSn, iid, pAVFormatInput->nb_streams);
        if (!streamList) {
            ret = AVERROR(ENOMEM);
            spdlog::error("evpusher {} {} failed create avformatcontext for output: %s", devSn, iid, av_err2str(AVERROR(ENOMEM)));
            exit(1);
        }

        int streamIdx = 0;
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
                spdlog::error("evpusher {} {} failed allocating output stream", devSn, iid);
                ret = AVERROR_UNKNOWN;

            }
            ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
            spdlog::info("evpusher {} {}  copied codepar", devSn, iid);
            if (ret < 0) {
                spdlog::error("evpusher {} {}  failed to copy codec parameters", devSn, iid);
            }
        }

        for(int i = 0; i < pAVFormatInput->nb_streams; i++ ) {
            spdlog::info("streamList[{:d}]: {:d}", i, streamList[i]);
        }

        av_dump_format(pAVFormatRemux, 0, urlOut.c_str(), 1);

        if (!(pAVFormatRemux->oformat->flags & AVFMT_NOFILE)) {
            spdlog::error("evpusher {} {} failed allocating output stream", devSn,iid);
            ret = avio_open2(&pAVFormatRemux->pb, urlOut.c_str(), AVIO_FLAG_WRITE, NULL, &pOptsRemux);
            if (ret < 0) {
                spdlog::error("evpusher {} {} could not open output file '%s'", devSn, iid, urlOut);
                exit(1);
            }
        }

        // rtsp tcp
        if(av_dict_set(&pOptsRemux, "rtsp_transport", "tcp", 0) < 0) {
            spdlog::error("evpusher {} {} failed set output pOptsRemux", devSn, iid);
            ret = AVERROR_UNKNOWN;
        }

        ret = avformat_write_header(pAVFormatRemux, &pOptsRemux);
        if (ret < 0) {
            spdlog::error("evpusher {} {} error occurred when opening output file", devSn, iid);
        }

        return ret;
    }

    void freeStream()
    {
        // close output context
        if(pAVFormatRemux) {
            if(pAVFormatRemux->pb) {
                avio_closep(&pAVFormatRemux->pb);
            }

            avformat_free_context(pAVFormatRemux);
        }
        pAVFormatRemux = NULL;
        // free avformatcontex
        if(pAVFormatInput != NULL) {
            AVFormatCtxSerializer::freeCtx(pAVFormatInput);
            pAVFormatInput = NULL;
        }

        pAVFormatInput = NULL;
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
            ret = zmq_recvmsg(pSub, &msg, 0);
            if(ret < 0) {
                spdlog::error("failed to recv zmq msg: {}", zmq_strerror(ret));
                continue;
            }

            if(pktCnt % EV_LOG_PACKET_CNT == 0) {
                spdlog::info("seq: {}, pts: {}, dts: {}, idx: {}", pktCnt, packet.pts, packet.dts, packet.stream_index);
            }

            pktCnt++;
            // decode
            ret = AVPacketSerializer::decode((char*)zmq_msg_data(&msg), ret, &packet);
            {
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

            /* copy packet */
            if(pktCnt == 0) {
                packet.pts = 0;
                packet.dts = 0;
                packet.duration = 0;
                packet.pos = -1;
            }
            else {
                packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);
                packet.pos = -1;
            }

            ret = av_interleaved_write_frame(pAVFormatRemux, &packet);
            av_packet_unref(&packet);
            if (ret < 0) {
                spdlog::error("error muxing packet: {}, {}, {}, {}, restreaming...", av_err2str(ret), packet.dts, packet.pts, packet.dts==AV_NOPTS_VALUE);
                if(pktCnt != 0 && packet.pts == AV_NOPTS_VALUE) {
                    // reset
                    av_write_trailer(pAVFormatRemux);
                    this_thread::sleep_for(chrono::seconds(5));
                    freeStream();
                    getInputFormat();
                    setupStream();
                    pktCnt = 0;
                    continue;
                }
            }
        }
        av_write_trailer(pAVFormatRemux);
        if(!bStopSig && ret < 0) {
            //TOOD: reconnect
            spdlog::error("TODO: failed, reconnecting");
        }
        else {
            spdlog::error("exit on command");
        }
    }

public:
    EvPusher()
    {
        init();
        setupMq();
        getInputFormat();
        setupStream();
    }

    ~EvPusher()
    {
        if(pSub != NULL) {
            zmq_close(pSub);
            pSub = NULL;
        }
        if(pSubCtx != NULL) {
            zmq_ctx_destroy(pSubCtx);
            pSubCtx = NULL;
        }
        if(pDealer != NULL) {
            zmq_close(pSub);
            pDealer = NULL;
        }
        if(pDealerCtx != NULL) {
            zmq_ctx_destroy(pSub);
            pDealerCtx = NULL;
        }

        freeStream();
    }
};

int main(int argc, char *argv[])
{
    av_log_set_level(AV_LOG_ERROR);
    spdlog::set_level(spdlog::level::info);
    EvPusher pusher;
    pusher.join();
}