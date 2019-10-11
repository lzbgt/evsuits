/*
module: evpuller
description:
author: Bruce.Lu <lzbgt@icloud.com>
created: 2019/08/23
update: 2019/09/10
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

#include "inc/zmqhelper.hpp"
#include "inc/tinythread.hpp"
#include "inc/common.hpp"
#include "inc/database.h"

using namespace std;
using namespace zmqhelper;

class EvPuller: public TinyThread {
private:
    void *pPubCtx = nullptr; // for packets publishing
    void *pPub = nullptr;
    void *pDealerCtx = nullptr;
    void *pDealer = nullptr;
    void *pDaemonCtx = nullptr, *pDaemon = nullptr;
    AVFormatContext *pAVFormatInput = nullptr;
    char *pAVFmtCtxBytes = nullptr;
    int lenAVFmtCtxBytes = 0;
    string urlIn, urlPub, urlDealer, mgrSn, devSn, selfId, ipcPort;
    int *streamList = nullptr, numStreams = 0, iid;
    time_t tsLastBoot, tsUpdateTime;
    json config;
    thread thEdgeMsgHandler, thCloudMsgHandler;
    string proto = "rtsp";
    string drport = "5549";
    condition_variable cvMsg;
    mutex mutMsg;
    bool gotFormat = false;

    int handleCloudMsg(vector<vector<uint8_t> > v)
    {
        int ret = 0;
        string peerId, metaType, metaValue, msg;
        json data;
        for(auto &b:v) {
            msg +=body2str(b) + ";";
        }

        bool bProcessed = false;
        if(v.size() == 3) {
            try {
                peerId = body2str(v[0]);
                json meta = json::parse(body2str(v[1]));
                metaType = meta["type"];
                if(meta.count("value") != 0) {
                    metaValue = meta["value"];
                }

                // msg from cluster mgr
                string daemonId = this->devSn + ":evdaemon:0";
                if(peerId == daemonId) {
                    if(metaValue == EV_MSG_META_VALUE_CMD_STOP || metaValue == EV_MSG_META_VALUE_CMD_RESTART) {
                        spdlog::info("evpuller {} received {} cmd from cluster mgr {}", selfId, metaValue, daemonId);
                        bProcessed = true;
                        exit(0);
                    }
                }
            }
            catch(exception &e) {
                spdlog::error("evpuller {} exception to process msg {}: {}", selfId, msg, e.what());
            }
        }

        if(!bProcessed) {
            spdlog::error("evpuller {} received msg having no implementation from peer: {}", selfId, msg);
        }

        return ret;
    }

    int handleEdgeMsg(vector<vector<uint8_t> > v)
    {
        int ret = 0;
        {
            unique_lock<mutex> lk(this->mutMsg);
            this->cvMsg.wait(lk, [this] {return this->gotFormat;});
        }

        spdlog::info("evpuller {} got inputformat", selfId);
        auto msgBody = data2body(const_cast<char*>(pAVFmtCtxBytes), lenAVFmtCtxBytes);
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
                }
                else {
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

    int ping()
    {
        int ret = 0;
        vector<vector<uint8_t> >body = {str2body(mgrSn + ":evmgr:0"), str2body(EV_MSG_META_PING), str2body(MSG_HELLO)};

        ret = z_send_multiple(pDealer, body);
        if(ret < 0) {
            spdlog::error("evpuller {} failed to send multiple: {}", selfId, zmq_strerror(zmq_errno()));
        }
        return ret;
    }

    int init()
    {
        bool inited = false;
        int ret = 0;
        bool found = false;
        string user, passwd, addr;
        try {
            spdlog::info("evpuller boot config: {} -> {}", selfId, config.dump());
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
            }
            else {
                ipcPort = to_string(ipc["port"]);
            }

            string ipcAddr = ipc["addr"].get<string>();
            if(strutils::isIpStr(ipcAddr)) {
                string chan = "ch1";
                string streamName = "main";
                if(ipc.count("channel") != 0 && !ipc["channel"].get<string>().empty()) {
                    chan = ipc["channel"].get<string>();
                }
                if(ipc.count("streamName") != 0 && !ipc["streamName"].get<string>().empty()) {
                    streamName = ipc["streamName"].get<string>();
                }

                if(ipc.count("proto") != 0 && !ipc["proto"].get<string>().empty()) {
                    proto = ipc["proto"];
                }

                urlIn = proto + "://" + user + ":" + passwd + "@" + ipc["addr"].get<string>() + ":" + ipcPort + "/h264/" + chan + "/" + streamName + "/av_stream";
            }
            else {
                urlIn = ipcAddr;
            }

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
                spdlog::error("evpuller {} {} failed setsockopts router: {}", selfId, urlDealer);
                exit(1);
            }
            ret = zmq_connect(pDealer, urlDealer.c_str());
            if(ret < 0) {
                spdlog::error("evpuller {} failed to connect to router {}", selfId, urlDealer);
                exit(1);
            }

            ping();
        }
        catch(exception &e) {
            this_thread::sleep_for(chrono::seconds(3));
            spdlog::error("evpuller {} exception in EvPuller.init {:s}, retrying... ", selfId, e.what());
            exit(1);
        }

        inited = true;
        spdlog::info("evpuller successfully load config");

        return 0;
    }

protected:
    // Function to be executed by thread function
    void run()
    {
        int ret = 0;
        AVDictionary * optsIn = nullptr;
        string proto = urlIn.substr(0,4);
        if(proto == "rtsp") {
            av_dict_set(&optsIn, "rtsp_transport", "tcp", 0);
        }
        else {
            //
        }

        spdlog::info("evpuller {} openning stream: {}", selfId, urlIn);
        if ((ret = avformat_open_input(&pAVFormatInput, urlIn.c_str(), NULL, &optsIn)) < 0) {
            spdlog::error("evpuller {} Could not open input stream {}", selfId, urlIn);
            exit(1);
        }

        spdlog::info("evpuller {} finding stream info: {}", selfId, urlIn);
        if ((ret = avformat_find_stream_info(pAVFormatInput, NULL)) < 0) {
            spdlog::error("evpuller {} Failed to retrieve input stream information", selfId);
            exit(1);
        }

        //pAVFormatInput->flags = AVFMT_FLAG_NOBUFFER | AVFMT_FLAG_FLUSH_PACKETS;

        numStreams = pAVFormatInput->nb_streams;
        int *streamList = (int *)av_mallocz_array(numStreams, sizeof(*streamList));

        if (!streamList) {
            ret = AVERROR(ENOMEM);
            spdlog::error("evpuller {} failed create avformatcontext for output: {}", selfId, av_err2str(AVERROR(ENOMEM)));
        }

        // serialize formatctx to bytes
        // be attention to the scope of lock guard!
        {
            lock_guard<mutex> lock(this->mutMsg);
            lenAVFmtCtxBytes = AVFormatCtxSerializer::encode(pAVFormatInput, &pAVFmtCtxBytes);
            if(lenAVFmtCtxBytes <= 0 || pAVFmtCtxBytes == nullptr) {
                spdlog::error("evpuller {} failed to pull packet from {}. exiting...", selfId, urlIn);
                exit(1);
            }
            gotFormat = true;
            cvMsg.notify_one();
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
                spdlog::error("evpuller {} failed read packet: {}", selfId, av_err2str(ret));
                break;
            }
            in_stream  = pAVFormatInput->streams[packet.stream_index];
            if (packet.stream_index >= numStreams || streamList[packet.stream_index] < 0) {
                av_packet_unref(&packet);
                continue;
            }
            if(pktCnt % EV_LOG_PACKET_CNT == 0) {
                spdlog::info("evpuller {} pktCnt: {:d}", selfId, pktCnt);
            }

            pktCnt++;
            packet.stream_index = streamList[packet.stream_index];

            // serialize packet to raw bytes
            char * data = nullptr;
            int size = AVPacketSerializer::encode(packet, &data);
            zmq_msg_init_data(&msg, (void*)data, size, mqPacketFree, NULL);
            //zmq_send_const(pPub, zmq_msg_data(&msg), size, 0);
            ret = zmq_msg_send(&msg, pPub, 0);

            av_packet_unref(&packet);
        }

        if(pAVFmtCtxBytes != nullptr) {
            free(pAVFmtCtxBytes);
        }

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
        const char *strEnv = getenv("DR_PORT");
        if(strEnv != nullptr) {
            drport = strEnv;
        }

        strEnv = getenv("PEERID");
        if(strEnv != nullptr) {
            selfId = strEnv;
            auto v = strutils::split(selfId, ':');
            if(v.size() != 3||v[1] != "evpuller") {
                spdlog::error("evpuller {} received invalid gid: {}", selfId);
                exit(1);
            }
            devSn = v[0];
            iid = stoi(v[2]);
        }
        else {
            spdlog::error("evpuller {} failed to start. no SN set", selfId);
            exit(1);
        }

        //
        string addr = string("tcp://127.0.0.1:") + drport;
        int ret = zmqhelper::setupDealer(&pDaemonCtx, &pDaemon, addr, selfId);
        if(ret != 0) {
            spdlog::error("evpuller {} failed to setup dealer {}", selfId, addr);
            exit(1);
        }

        ret = zmqhelper::recvConfigMsg(pDaemon, config, addr, selfId);
        if(ret != 0) {
            spdlog::error("evpuller {} failed to receive configration message {}", selfId, addr);
        }
        init();

        thEdgeMsgHandler = thread([this] {
            while(true)
            {
                auto body = z_recv_multiple(pDealer,false);
                if(body.size() == 0) {
                    spdlog::error("evslicer {} failed to receive multiple msg: {}", selfId, zmq_strerror(zmq_errno()));
                    continue;
                }
                // full proto msg received.
                string msg;
                for(auto &v: body) {
                    msg += body2str(v) + ",";
                }
                spdlog::info("evpuller {} received edge msg: {}", selfId, msg);
                this->handleEdgeMsg(body);
            }
        });
        thEdgeMsgHandler.detach();


        thCloudMsgHandler = thread([this] {
            while(true)
            {
                auto body = z_recv_multiple(pDaemon,false);
                if(body.size() == 0) {
                    spdlog::error("evslicer {} failed to receive multiple msg: {}", selfId, zmq_strerror(zmq_errno()));
                    continue;
                }
                // full proto msg received.
                this->handleCloudMsg(body);
            }
        });
        thCloudMsgHandler.detach();
    }

    ~EvPuller()
    {
        if(pPub != nullptr) {
            zmq_close(pPub);
            pPub = nullptr;
        }
        if(pPubCtx != nullptr) {
            zmq_ctx_destroy(pPubCtx);
            pPubCtx = nullptr;
        }
        if(pDealer != nullptr) {
            zmq_close(pDealer);
            pDealer= nullptr;
        }
        if(pDealerCtx != nullptr) {
            zmq_ctx_destroy(pPubCtx);
            pDealerCtx = nullptr;
        }
    }
};

int main(int argc, char **argv)
{
    av_log_set_level(AV_LOG_INFO);
    spdlog::set_level(spdlog::level::info);
    //DB::exec(NULL, NULL, NULL,NULL);
    auto evp = EvPuller();
    evp.join();
    return 0;
}