/*
module: evpuller
description:
author: Bruce.Lu <lzbgt@icloud.com>
update: 2019/08/23
*/

#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic ignored "-Wunused-private-field"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

#include <stdlib.h>
#include <string>
#include <thread>
#include <iostream>
#include <chrono>
#include <future>
#include <ctime>

#ifdef OS_LINUX
#include <filesystem>
namespace fs = std::filesystem;
#endif

#include "inc/zmqhelper.hpp"
#include "inc/tinythread.hpp"
#include "inc/common.hpp"
#include "inc/database.h"

using namespace std;
using namespace zmqhelper;

class RepSrv: public TinyThread {
private:
    string mgrSn;
    string devSn;
    string selfId;
    int iid;
    string urlRep;
    const char * bytes;
    int len;
    void *pDealer=NULL;
    thread thPing;

    int ping()
    {
        int ret = 0;
        vector<vector<uint8_t> >body;
        // since identity is auto set
        body.push_back(str2body(mgrSn + ":0:0"));
        body.push_back(str2body(EV_MSG_META_PING));
        body.push_back(str2body(MSG_HELLO));

        ret = z_send_multiple(pDealer, body);
        if(ret < 0) {
            spdlog::error("evpuller {} failed to send multiple: {}", selfId, zmq_strerror(zmq_errno()));
        }
        return ret;
    }

    int handleMsg(vector<vector<uint8_t> > v)
    {
        int ret = 0;
        auto msgBody = data2body(const_cast<char*>(bytes), len);
        try {
            // rep framectx
            // TODO: verify sender id
            string sMeta = body2str(v[1]);
            string peerId = body2str(v[0]);
            auto meta = json::parse(sMeta);
            if(meta["type"].get<string>() == EV_MSG_META_AVFORMATCTX) {
                vector<vector<uint8_t> > rep = {v[0], v[1], msgBody};
                ret = z_send_multiple(pDealer, rep);
                if(ret < 0) {
                    spdlog::error("evpuller {} failed to send avformatctx data to requester {}: {}", selfId, peerId, zmq_strerror(zmq_errno()));
                }else{
                    spdlog::info("evpuller {} success to send avformatctx data to requester {}", selfId, peerId);
                }
            }
            else if(meta["type"].get<string>() == EV_MSG_META_EVENT) {
                // event msg
                spdlog::info("evpuller {} received event: {}", selfId, body2str(v[2]));
            }
            else {
                spdlog::error("evpuller {} unknown meta from {}: {}", selfId, body2str(v[0]), body2str(v[1]));
            }
        }
        catch(exception &e) {
            spdlog::error("evpuller {} excpetion parse request from {}: {}", selfId, body2str(v[0]), body2str(v[1]));
        }

        return ret;
    }
protected:
    void run()
    {
        int ret = 0;
        bool bStopSig = false;
        // declare ready to router
        ping();

        // TODO: don't need this anymore, since I've used the draft feature of ZOUTER_NOTIFICATION instead
        // thPing = thread([&,this]() {
        //     while(true) {
        //         this_thread::sleep_for(chrono::seconds(EV_HEARTBEAT_SECONDS-2));
        //         ping();
        //     }
        // });

        // thPing.detach();

        // init response msg
        while (true) {
            if(checkStop() == true) {
                bStopSig = true;
                break;
            }

            spdlog::info("evpuller {} waiting for req", selfId);
            // proto: [sender_id] [meta] [body]
            auto v = z_recv_multiple(pDealer, false);
            if(v.size() != 3) {
                //TODO:
                spdlog::error("evpuller {},  repSrv received invalid message: {}", selfId, v.size());
                continue;
            }
            handleMsg(v);
        }
    }
public:
    RepSrv() = delete;
    RepSrv(RepSrv &) = delete;
    RepSrv(RepSrv&&) = delete;
    RepSrv(string mgrSn, string devSn, int iid, const char* formatBytes,
           int len, void *pDealer):mgrSn(mgrSn),devSn(devSn), iid(iid), bytes(formatBytes),
        len(len), pDealer(pDealer) {
            selfId = devSn+":evpuller:" + to_string(iid);
        };

    ~RepSrv() {};
};

class EvPuller: public TinyThread {
private:
    void *pPubCtx = NULL; // for packets publishing
    void *pPub = NULL;
    void *pDealerCtx = NULL;
    void *pDealer = NULL;
    void *pDaemonCtx = NULL, *pDaemon = NULL;
    AVFormatContext *pAVFormatInput = NULL;
    string urlIn, urlPub, urlDealer, mgrSn, devSn, selfId, ipcPort;
    int *streamList = NULL, numStreams = 0, iid;
    time_t tsLastBoot, tsUpdateTime;
    json config;

    int init()
    {
        bool inited = false;
        int ret = 0;

        spdlog::info("evpuller: {}", selfId);
            
        bool found = false;
        string user, passwd, addr;
        try {
            spdlog::info("config dump: {:s}", config.dump());
            json evpuller;
            json &evmgr = this->config;
            json ipc;
            json ipcs = evmgr["ipcs"];
            for(auto &j: ipcs) {
                json pullers = j["modules"]["evpuller"];
                for(auto &p:pullers) {
                    if(p["sn"] == devSn && p["enabled"] != 0 && p["iid"] == iid) {
                        evpuller = p;
                        break;
                    }
                }
                if(evpuller.size() != 0) {
                    ipc = j;
                    break;
                }
            }

            if(ipc.size()!=0 && evpuller.size()!=0) {
                found = true;
            }
           

            if(!found) {
                spdlog::error("evpuller {} no valid config found", devSn);
                exit(1);
            }

            mgrSn = evmgr["sn"];
            user = ipc["user"];
            passwd = ipc["password"];

            // default stream port
            if(ipc.count("port") == 0) {
                ipcPort = "554";
            }else{
                ipcPort = to_string(ipc["port"]);
            }

            urlIn = "rtsp://" + user + ":" + passwd + "@" + ipc["addr"].get<string>() + ":" + ipcPort + "/h264/ch1/sub/av_stream";
            addr = evpuller["addr"].get<string>();
            spdlog::info("evpuller {} connecting to IPC {}", selfId, urlIn);
            if(addr == "*" || addr == "0.0.0.0") {
                spdlog::error("evpuller {} invalid addr {} for pub", selfId, evpuller.dump());
                exit(1);
            }

            urlPub = string("tcp://*:") + to_string(evpuller["port-pub"]);
            urlDealer = "tcp://" + evmgr["addr"].get<string>() + string(":") + to_string(evmgr["port-router"]);
            spdlog::info("evpuller {} bind on {} for pub, connect to {} for dealer", selfId, urlPub, urlDealer);

            pPubCtx = zmq_ctx_new();
            pPub = zmq_socket(pPubCtx, ZMQ_PUB);
            ret = zmq_bind(pPub, urlPub.c_str());
            if(ret < 0) {
                spdlog::error("evpuller {} failed to bind to {}", selfId, urlPub);
                exit(1);
            }
            pDealerCtx = zmq_ctx_new();
            pDealer = zmq_socket(pDealerCtx, ZMQ_DEALER);
            ret = zmq_setsockopt(pDealer, ZMQ_IDENTITY, selfId.c_str(), selfId.size());
            if(ret < 0) {
                spdlog::error("evpuller {} failed to set identity", selfId);
                exit(1);
            }
            ret += zmq_setsockopt (pDealer, ZMQ_ROUTING_ID, selfId.c_str(), selfId.size());
            if(ret < 0) {
                spdlog::error("evpusher {} {} failed setsockopts router: {}", selfId, urlDealer);
                exit(1);
            }
            ret = zmq_connect(pDealer, urlDealer.c_str());
            if(ret < 0) {    
                spdlog::error("evpuller {} failed to connect to router {}", selfId, urlDealer);
                exit(1);
            }
        }
        catch(exception &e) {
            this_thread::sleep_for(chrono::seconds(3));
            spdlog::error("evpuller {} exception in EvPuller.init {:s}, retrying... ", selfId, e.what());
            exit(1);
        }

        inited = true;
        spdlog::info("successfully load config");

        return 0;
    }

protected:
    // Function to be executed by thread function
    void run()
    {
        int ret = 0;
        AVDictionary * optsIn;
        av_dict_set(&optsIn, "timeout", "7", 0);
        if ((ret = avformat_open_input(&pAVFormatInput, urlIn.c_str(), NULL, NULL)) < 0) {
            spdlog::error("Could not open input stream {}", urlIn);
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

        // serialize formatctx to bytes
        char *pBytes = NULL;
        ret = AVFormatCtxSerializer::encode(pAVFormatInput, &pBytes);
        auto repSrv = RepSrv(mgrSn, devSn, iid, pBytes, ret, pDealer);
        repSrv.detach();

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
        uint64_t pktCnt = 0;
        spdlog::info("evpulelr {} reading packets from {}", selfId, urlIn);
        while (true) {
            if(checkStop() == true) {
                bStopSig = true;
                break;
            }

            // if(1 == getppid()) {
            //     spdlog::error("evpuller {} exit since evdaemon is dead", selfId);
            //     exit(1);
            // }

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
            if(pktCnt % EV_LOG_PACKET_CNT == 0) {
                spdlog::info("pktCnt: {:d}", pktCnt);
            }

            pktCnt++;
            packet.stream_index = streamList[packet.stream_index];

            // serialize packet to raw bytes
            char * data = NULL;
            int size = AVPacketSerializer::encode(packet, &data);
            zmq_msg_init_data(&msg, (void*)data, size, mqPacketFree, NULL);
            zmq_send_const(pPub, zmq_msg_data(&msg), size, 0);

            av_packet_unref(&packet);
        }

        free(pBytes);
        // TODO:
        if(ret < 0 && !bStopSig) {
            // reconnect
        }
        else {
            std::cout << "Task End" << std::endl;
        }
    }

public:
    EvPuller()
    {
        string drport;
        const char *strEnv = getenv("DR_PORT");
        if(strEnv != NULL) {
            drport = strEnv;
        }else{
            spdlog::error("evpusher failed to start. no DR_PORT set");
            exit(1);
        }

        strEnv = getenv("GID");
        if(strEnv != NULL) {
            selfId = strEnv;
            auto v = strutils::split(selfId, ':');
            if(v.size() != 3||v[1] != "evpuller") {
                spdlog::error("evpusher received invalid gid: {}", selfId);
                exit(1);
            }
            devSn = v[0];
            iid = stoi(v[2]);
        }else{
            spdlog::error("evpusher failed to start. no SN set");
            exit(1);
        }

        //
        string addr = string("tcp://127.0.0.1:") + drport;
        int ret = zmqhelper::setupDealer(&pDaemonCtx, &pDaemon, addr, selfId);
        if(ret != 0) {
            spdlog::error("evpusher {} failed to setup dealer {}", devSn, addr);
            exit(1);
        }

        ret = zmqhelper::recvConfigMsg(pDaemon, config, addr, selfId);
        if(ret != 0) {
            spdlog::error("evpusher {} failed to receive configration message {}", devSn , addr);
        }
        init();
    }

    ~EvPuller()
    {
        if(pPub != NULL) {
            zmq_close(pPub);
            pPub = NULL;
        }
        if(pPubCtx != NULL) {
            zmq_ctx_destroy(pPubCtx);
            pPubCtx = NULL;
        }
        if(pDealer != NULL) {
            zmq_close(pDealer);
            pDealer= NULL;
        }
        if(pDealerCtx != NULL) {
            zmq_ctx_destroy(pPubCtx);
            pDealerCtx = NULL;
        }
    }
};

int main(int argc, char **argv)
{
    av_log_set_level(AV_LOG_ERROR);
    spdlog::set_level(spdlog::level::debug);
    //DB::exec(NULL, NULL, NULL,NULL);
    auto evp = EvPuller();
    evp.join();
    return 0;
}