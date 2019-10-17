/*
module: evslicer
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
#include <vector>
#include <ctime>
#include <functional>
#include <queue>
#include <fstream>
#include <algorithm>
#include <set>

#include <cstdlib>
#include "inc/zmqhelper.hpp"
#include "inc/tinythread.hpp"
#include "inc/common.hpp"
#include "inc/database.h"
#include "postfile.h"
#include "dirmon.h"
#include "inc/fs.h"

using namespace std;
using namespace zmqhelper;

class EvSlicer: public TinyThread {
private:
#define URLOUT_DEFAULT "slices"
#define NUM_HOURS_DEFAULT 2
#define SECONDS_PER_SLICE_DEFAULT 30
// 2 hours, 30 seconds per record
    void *pSubCtx = nullptr, *pDealerCtx = nullptr; // for packets relay
    void *pSub = nullptr, *pDealer = nullptr, *pDaemonCtx = nullptr, *pDaemon = nullptr;
    string urlOut, urlPub, urlRouter, devSn, mgrSn, selfId, pullerGid, ipcSn;
    int iid, hours, seconds, numSlices;
    long bootTime = 0;
    bool enablePush = false;
    AVFormatContext *pAVFormatRemux = nullptr;
    AVFormatContext *pAVFormatInput = nullptr;
    AVDictionary *pOptsRemux = nullptr;
    int *streamList = nullptr;
    time_t tsLastBoot, tsUpdateTime;
    json config;
    thread thEdgeMsgHandler, thCloudMsgHandler, thSliceMgr;
    string drport = "5549";
    json slices;
    condition_variable cvMsg;
    mutex mutMsg;
    bool gotFormat = false;
    queue<string> eventQueue;
    condition_variable cvEvent;
    mutex mutEvent;
    thread thEventHandler;
    string videoFileServerApi = "http://139.219.142.18:10008/upload/evtvideos/";
    set<long> sTsList;
    mutex mutTsList;


    bool validMsg(json &msg)
    {
        return true;
    }
    int handleEdgeMsg(vector<vector<uint8_t> > v)
    {
        int ret = 0;
        string peerId, meta;
        json data;
        string msg;
        for(auto &b:v) {
            msg +=body2str(b) + ";";
        }

        if(v.size() == 3) {
            try {
                peerId = body2str(v[0]);
                meta = json::parse(body2str(v[1]))["type"];
                if(meta == EV_MSG_META_AVFORMATCTX) {
                    lock_guard<mutex> lock(this->mutMsg);
                    if(pAVFormatInput == nullptr) {
                        pAVFormatInput = (AVFormatContext *)malloc(sizeof(AVFormatContext));
                        AVFormatCtxSerializer::decode((char *)(v[2].data()), v[2].size(), pAVFormatInput);
                        gotFormat = true;
                        cvMsg.notify_one();
                        spdlog::info("evslicer {} got avformat from {}", selfId, peerId);
                    }else{
                        spdlog::warn("evslicer {} received avformatctx msg from {}, but already proceessed before, ignored. TODO: reinit", selfId, peerId);
                    }                    
                }
                else if(meta == EV_MSG_META_EVENT) {
                    data = json::parse(body2str(v[2]));

                    /// evslicer has two msg interfaces to subsystems on edge side
                    /// 1. type = "event";  start: timestamp; end: timestamp
                    /// 2. type = "media"; duration: seconds
                    if(!validMsg(data)) {
                        spdlog::info("evslicer {} received invalid msg from {}: {}", selfId, peerId, msg);
                    }
                    else {
                        spdlog::info("evslicer {} received msg from {}, type = {}, data = {}", selfId, peerId, meta, data.dump());
                        if(data["type"] == "event") {
                            lock_guard<mutex> lock(this->mutEvent);
                            eventQueue.push(data.dump());
                            spdlog::info("evslicer {} event num: {}", selfId, eventQueue.size());
                            if(eventQueue.size() > MAX_EVENT_QUEUE_SIZE) {
                                eventQueue.pop();
                            }
                            cvEvent.notify_one();
                        }
                        else {
                            spdlog::error("evslicer {} msg not supported from {}: {}", selfId, peerId, msg);
                        }
                    }
                }
                else {
                    spdlog::info("evslicer {} received unkown msg from {}: {}", selfId, peerId, msg);
                }
            }
            catch(exception &e) {
                spdlog::error("evslicer {} exception to process msg {}: {}", selfId, msg, e.what());
            }
        }
        else {
            spdlog::error("evslicer {} get invalid msg with size {}: {}", selfId, v.size(), msg);
        }

        return ret;
    }

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
                string daemonId = this->devSn + ":evdaemon:0";

                // msg from cluster mgr
                if(peerId == daemonId) {
                    if(metaValue == EV_MSG_META_VALUE_CMD_STOP || metaValue == EV_MSG_META_VALUE_CMD_RESTART) {
                        spdlog::info("evslicer {} received {} cmd from cluster mgr {}", selfId, metaValue, daemonId);
                        bProcessed = true;
                        exit(0);
                    }else if(metaValue == "debug:record") {
                        try{
                            json body = json::parse(body2str(v[2]));
                            if(body.count("data") != 0 && body["data"].is_object() && body["data"].count("start") != 0 && body["data"]["start"].is_number() && body["data"].count("end") != 0 && body["data"]["end"].is_number()) {
                                json evt;
                                evt["type"] = "event";
                                evt["start"] = body["data"]["start"];
                                evt["end"] = body["data"]["end"];
                                {
                                    lock_guard<mutex> lock(this->mutEvent);
                                    eventQueue.push(evt.dump());
                                    cvEvent.notify_one();
                                }       
                                bProcessed = true;
                            }
                        }catch(exception &e) {
                            spdlog::error("evslicer {} exception in handleCloudMsg: {}", selfId, e.what());
                        }
                    }else if(metaValue == "debug:xxx"){
                        // TODO: remove debug feature
                        bProcessed = true;
                    }else if(metaValue == "debug:list_files"){
                        // TODO: remove debug feature
                        printVideoFiles(this->sTsList);
                        bProcessed = true;
                    }else if(metaValue == "debug:toggle_log") {
                        // TODO: remove debug feature
                        static bool toggle = false;
                        toggle = !toggle;
                        if(toggle){
                            spdlog::set_level(spdlog::level::debug);
                        }else{
                            spdlog::set_level(spdlog::level::info);
                        }

                        bProcessed = true;
                    }
                }
            }
            catch(exception &e) {
                spdlog::error("evslicer {} exception to process msg {}: {}", selfId, msg, e.what());
            }
        }

        if(!bProcessed) {
            spdlog::error("evslicer {} received msg having no implementation from peer: {}", selfId, msg);
        }

        return ret;
    }

    int init()
    {
        int ret = 0;
        bool found = false;
        try {
            spdlog::info("evslicer boot config: {} -> {}", selfId, config.dump());
            json evslicer;
            json &evmgr = this->config;
            json ipc;

            json ipcs = evmgr["ipcs"];
            for(auto &j: ipcs) {
                json pullers = j["modules"]["evslicer"];
                for(auto &p:pullers) {
                    if(p["sn"] == devSn && p["enabled"] != 0 && p["iid"] == iid) {
                        evslicer = p;
                        break;
                    }
                }
                if(evslicer.size() != 0) {
                    ipc = j;
                    break;
                }
            }

            if(ipc.size()!=0 && evslicer.size()!=0) {
                found = true;
            }

            if(!found) {
                spdlog::error("evslicer {}: no valid config found. retrying load config...", devSn);
                exit(1);
            }

            selfId = devSn + ":evslicer:" + to_string(iid);

            //
            if(ipc.count("sn") == 0) {
                ipcSn = "unkown";
            }
            else {
                ipcSn = ipc["sn"];
            }

            if(evslicer.count("videoServerAddr") != 0  && !evslicer["videoServerAddr"].get<string>().empty()) {
                videoFileServerApi = evslicer["videoServerAddr"].get<string>();
                if(videoFileServerApi.at(videoFileServerApi.size()-1) != '/') {
                    videoFileServerApi += string("/");
                }
            }

            this->videoFileServerApi += this->ipcSn;

            json evpuller = ipc["modules"]["evpuller"][0];
            pullerGid = evpuller["sn"].get<string>() + ":evpuller:" + to_string(evpuller["iid"]);
            mgrSn = evmgr["sn"];

            if(evslicer.count("path") == 0) {
                spdlog::info("evslicer {} no params for path, using default: {}", selfId, URLOUT_DEFAULT);
                urlOut = URLOUT_DEFAULT;
            }
            else {
                urlOut = evslicer["path"];
            }

            if(evslicer.count("hours") == 0) {
                spdlog::info("evslicer {} no params for hours, using default: {}", selfId, NUM_HOURS_DEFAULT);
                hours = NUM_HOURS_DEFAULT;
            }
            else {
                hours = evslicer["hours"].get<int>();
            }

            if(evslicer.count("seconds") == 0) {
                spdlog::info("evslicer {} no params for seconds, using default: {}", selfId, SECONDS_PER_SLICE_DEFAULT);
                seconds = SECONDS_PER_SLICE_DEFAULT;
            }
            else {
                seconds = evslicer["seconds"].get<int>();
            }

            numSlices = hours * 60 * 60 /seconds;

            spdlog::info("evslicer mkdir -p {}", selfId, urlOut);
            ret = system((string("mkdir -p ") + urlOut).c_str());


            int portPub = 5556;
            if(evpuller.count("portPub") != 0 && evpuller["portPub"].is_number_integer()) {
                portPub = evpuller["portPub"];
            }else if(evpuller.count("port-pub") != 0 && evpuller["port-pub"].is_number_integer()){
                portPub = evpuller["port-pub"];
            }

            int portRouter = 5550;
            if(evmgr.count("portRouter") != 0 && evmgr["portRouter"].is_number_integer()) {
                portRouter = evmgr["portRouter"];
            }else if(evmgr.count("port-router") != 0 && evmgr["port-router"].is_number_integer()) {
                portRouter = evmgr["port-router"];
            }

            urlPub = string("tcp://") + evpuller["addr"].get<string>() + ":" + to_string(portPub);
            urlRouter = string("tcp://") + evmgr["addr"].get<string>() + ":" + to_string(portRouter);
            spdlog::info("evslicer {} will connect to {} for sub, {} for router", selfId, urlPub, urlRouter);

            // setup sub
            pSubCtx = zmq_ctx_new();
            pSub = zmq_socket(pSubCtx, ZMQ_SUB);
            //ZMQ_TCP_KEEPALIVE
            //ZMQ_TCP_KEEPALIVE_IDLE
            //ZMQ_TCP_KEEPALIVE_INTVL
            ret = 1;
            zmq_setsockopt(pSub, ZMQ_TCP_KEEPALIVE, &ret, sizeof (ret));
            ret = 5;
            zmq_setsockopt(pSub, ZMQ_TCP_KEEPALIVE_IDLE, &ret, sizeof (ret));
            zmq_setsockopt(pSub, ZMQ_TCP_KEEPALIVE_INTVL, &ret, sizeof (ret));
            ret = zmq_setsockopt(pSub, ZMQ_SUBSCRIBE, "", 0);
            if(ret != 0) {
                spdlog::error("evslicer {} failed set setsockopt: {}", selfId, urlPub);
                exit(1);
            }

            ret = zmq_connect(pSub, urlPub.c_str());
            if(ret != 0) {
                spdlog::error("evslicer {} failed connect pub: {}", selfId, urlPub);
                exit(1);
            }

            // setup dealer
            pDealerCtx = zmq_ctx_new();
            pDealer = zmq_socket(pDealerCtx, ZMQ_DEALER);
            spdlog::info("evslicer {} try create req to {}", selfId, urlRouter);
            //ZMQ_TCP_KEEPALIVE
            //ZMQ_TCP_KEEPALIVE_IDLE
            //ZMQ_TCP_KEEPALIVE_INTVL
            ret = 1;
            zmq_setsockopt(pDealer, ZMQ_TCP_KEEPALIVE, &ret, sizeof (ret));
            ret = 5;
            zmq_setsockopt(pDealer, ZMQ_TCP_KEEPALIVE_IDLE, &ret, sizeof (ret));
            zmq_setsockopt(pDealer, ZMQ_TCP_KEEPALIVE_INTVL, &ret, sizeof (ret));
            ret = zmq_setsockopt(pDealer, ZMQ_IDENTITY, selfId.c_str(), selfId.size());
            ret += zmq_setsockopt (pDealer, ZMQ_ROUTING_ID, selfId.c_str(), selfId.size());
            if(ret < 0) {
                spdlog::error("evslicer {} {} failed setsockopts router: {}", selfId, urlRouter);
                exit(1);
            }
            if(ret < 0) {
                spdlog::error("evslicer {} failed setsockopts router: {}", selfId, urlRouter);
                exit(1);
            }
            ret = zmq_connect(pDealer, urlRouter.c_str());
            if(ret != 0) {
                spdlog::error("evslicer {} failed connect dealer: {}", selfId, urlRouter);
                exit(1);
            }
            //ping
            ret = ping();
        }
        catch(exception &e) {
            spdlog::error("evslicer {} exception in init {:s} retrying", selfId, e.what());
            exit(1);
        }

        return ret;
    }

    int ping()
    {
        // send hello to router
        int ret = 0;
        /// identity is auto set
        vector<vector<uint8_t> >body = {str2body(mgrSn+":evmgr:0"), str2body(EV_MSG_META_PING), str2body(MSG_HELLO)};
        ret = z_send_multiple(pDealer, body);
        if(ret < 0) {
            spdlog::error("evslicer {} failed to send multiple: {}", selfId, zmq_strerror(zmq_errno()));
            //TODO:
        }
        else {
            spdlog::info("evslicer {} sent hello to router: {}", selfId, mgrSn);
        }

        return ret;
    }

    int getInputFormat()
    {
        int ret = 0;
        // req avformatcontext packet
        // send hello to puller
        spdlog::info("evslicer {} send hello to puller: {}", selfId, pullerGid);
        vector<vector<uint8_t> > body;
        body.push_back(str2body(pullerGid));
        json meta;
        meta["type"] = EV_MSG_META_AVFORMATCTX;
        body.push_back(str2body(meta.dump()));
        body.push_back(str2body(MSG_HELLO));

        this->gotFormat = false;

        ret = z_send_multiple(pDealer, body);
        if(ret < 0) {
            spdlog::error("evslicer {}, failed to send hello to puller: {}. exiting ...", selfId, zmq_strerror(zmq_errno()));
            exit(1);
        }
        unique_lock<mutex> lk(this->mutMsg);
        this->cvMsg.wait(lk, [this] {return this->gotFormat;});

        return ret;
    }

    int setupStream()
    {
        int ret = 0;
        int streamIdx = 0;
        // find all video & audio streams for remuxing
        streamList = (int *)av_mallocz_array(pAVFormatInput->nb_streams, sizeof(*streamList));
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
        }

        for(int i = 0; i < pAVFormatInput->nb_streams; i++ ) {
            spdlog::info("evslicer {} streamList[{:d}]: {:d}", selfId, i, streamList[i]);
        }

        //av_dict_set(&pOptsRemux, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
        av_dict_set(&pOptsRemux, "c:v", "libx264", 0);
        //av_dict_set(&pOptsRemux, "brand", "mp42", 0);
        //av_dict_set(&pOptsRemux, "movflags", "faststart", 0);
        av_dict_set(&pOptsRemux, "strftime", "1", 0);
        av_dict_set(&pOptsRemux, "segment_format", "mp4", 0);
        av_dict_set(&pOptsRemux, "f", "segment", 0);
        av_dict_set(&pOptsRemux, "segment_time", to_string(seconds).data(), 0);
        av_dict_set(&pOptsRemux, "segment_wrap", to_string(numSlices).data(), 0);

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
        pAVFormatRemux = nullptr;
        // free avformatcontex
        if(pAVFormatInput != nullptr) {
            AVFormatCtxSerializer::freeCtx(pAVFormatInput);
            pAVFormatInput = nullptr;
        }

        pAVFormatInput = nullptr;
    }

protected:
    void run()
    {
        bool bStopSig = false;
        int ret = 0;
        int idx = 0;
        int pktCnt = 0;
        AVStream * out_stream = nullptr;
        zmq_msg_t msg;
        AVPacket packet;
        while (true) {
            auto start = chrono::system_clock::now();
            auto end = start;
            int ts = chrono::duration_cast<chrono::seconds>(start.time_since_epoch()).count();
            string name = to_string(ts) + ".mp4";
            name = urlOut + "/" + "%Y%m%d_%H%M%S.mp4";
            ret = avformat_alloc_output_context2(&pAVFormatRemux, NULL, "segment", name.c_str());
            if (ret < 0) {
                spdlog::error("evslicer {} failed create avformatcontext for output: %s", selfId, av_err2str(ret));
                exit(1);
            }

            // build output avformatctx
            for(int i =0; i < pAVFormatInput->nb_streams; i++) {
                if(streamList[i] != -1) {
                    out_stream = avformat_new_stream(pAVFormatRemux, NULL);
                    if (!out_stream) {
                        spdlog::error("evslicer {} failed allocating output stream {}", selfId, i);
                        ret = AVERROR_UNKNOWN;
                    }
                    ret = avcodec_parameters_copy(out_stream->codecpar, pAVFormatInput->streams[i]->codecpar);
                    if (ret < 0) {
                        spdlog::error("evslicer {} failed to copy codec parameters", selfId);
                    }
                }
            }

            if (!(pAVFormatRemux->oformat->flags & AVFMT_NOFILE)) {
                ret = avio_open2(&pAVFormatRemux->pb, name.c_str(), AVIO_FLAG_WRITE, NULL, &pOptsRemux);
                if (ret < 0) {
                    spdlog::error("evslicer {} could not open output file {}", selfId, name);
                }
            }
            // TODO
            av_dict_set(&pOptsRemux, "segment_start_number", to_string(this->sTsList.size()).data(), 0);
            ret = avformat_write_header(pAVFormatRemux, &pOptsRemux);
            if (ret < 0) {
                spdlog::error("evslicer {} error occurred when opening output file", selfId);
            }

            bootTime = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
            spdlog::info("evslicer {} start writing new slices", selfId);
            int pktIgnore = 0;
            while(true) {
                int ret =zmq_msg_init(&msg);
                ret = zmq_recvmsg(pSub, &msg, 0);
                if(ret < 0) {
                    spdlog::error("evslicer {} failed to recv zmq msg: {}",selfId, zmq_strerror(ret));
                    continue;
                }
                ret = AVPacketSerializer::decode((char*)zmq_msg_data(&msg), ret, &packet);
                {
                    if (ret < 0) {
                        spdlog::error("evslicer {} packet decode failed: {}", selfId, ret);
                        continue;
                    }
                }
                zmq_msg_close(&msg);

                if(pktCnt == 0 && pktIgnore < 18*7) {
                    pktIgnore++;
                    av_packet_unref(&packet);
                    continue;
                }

                AVStream *in_stream = nullptr, *out_stream = nullptr;
                in_stream  = pAVFormatInput->streams[packet.stream_index];
                packet.stream_index = streamList[packet.stream_index];
                out_stream = pAVFormatRemux->streams[packet.stream_index];
                //calc pts

                if(pktCnt % EV_LOG_PACKET_CNT == 0) {
                    spdlog::info("evslicer {} seq: {}, pts: {}, dts: {}, idx: {}", selfId, pktCnt, packet.pts, packet.dts, packet.stream_index);
                }
                /* copy packet */
                if(pktCnt == 0) {
                    packet.pts = AV_NOPTS_VALUE;
                    packet.dts = AV_NOPTS_VALUE;
                    packet.duration = 0;
                    packet.pos = -1;
                }
                else {
                    packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                    packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                    packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);
                    packet.pos = -1;
                }
                pktCnt++;

                ret = av_interleaved_write_frame(pAVFormatRemux, &packet);
                av_packet_unref(&packet);
                if (ret < 0) {
                    spdlog::error("evslicer {} error muxing packet: {}, {}, {}, {}, reloading...", selfId, av_err2str(ret), packet.dts, packet.pts, packet.dts==AV_NOPTS_VALUE);
                    if(pktCnt != 0 && packet.pts == AV_NOPTS_VALUE) {
                        // reset
                        av_write_trailer(pAVFormatRemux);
                        this_thread::sleep_for(chrono::seconds(5));
                        freeStream();
                        getInputFormat();
                        setupStream();
                        pktCnt = 0;
                        break;
                    }
                }

                end = chrono::system_clock::now();
            }
            if (pAVFormatRemux != nullptr) {
                if(pAVFormatRemux->pb != nullptr) {
                    avio_closep(&pAVFormatRemux->pb);
                }
                avformat_free_context(pAVFormatRemux);
            }

        }// outer while

    }

    string getBaseName(const string &fname)
    {
        string ret;
        auto posS = fname.find_last_of('/');
        if(posS == string::npos) {
            posS = 0;
        }
        else {
            posS = posS +1;
        }
        auto posE = fname.find_last_of('.');
        if(posE == string::npos) {
            posE = fname.size()-1;
        }
        else {
            posE = posE -1;
        }
        if(posE < posS) {
            spdlog::error("evslicer getBaseName invalid filename");
            return ret;
        }

        return fname.substr(posS, posE - posS + 1);
    }

    long videoFileName2Ts(string &fileBaseName)
    {
        std::tm t;
        strptime(fileBaseName.data(), "%Y%m%d_%H%M%S", &t);
        // disable dst
        t.tm_isdst = -1; //0 - utc, 1 dst, -1 system local.
        return mktime(&t);
    }

    string videoFileTs2Name(long ts, bool bLog = false)
    {
        std::tm t;
        memcpy(&t, localtime(&ts), sizeof(t));
        //t.tm_isdst = -1;

        char buffer[20];
        // Format: Mo, 15.06.2009 20:20:00
        std::strftime(buffer, 20, "%Y%m%d_%H%M%S", &t);
        if(bLog)
            spdlog::info("ts: {}, fname: {}/{}.mp4", ts, this->urlOut, buffer);
        return string(buffer);
    }

    void printVideoFiles(set<long> &list)
    {
        spdlog::info("evslicer {} debug files ring. size: {} max: {}",this->selfId, list.size(), this->numSlices);
        // lock_guard<mutex> lg(mutTsList);
        for(auto i: list) {
            spdlog::info("\tevslicer {} file ts: {}, baseName: {}", selfId, i, videoFileTs2Name(i));
        }
    }

    void loadVideoFiles(string path, int hours, int maxSlices, set<long> &_list)
    {
        auto now = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
        try {
            string fname, baseName;
            for (const auto & entry : fs::directory_iterator(path)) {
                fname = entry.path().c_str();
                if(entry.file_size() == 0 || !entry.is_regular_file()||entry.path().extension() != ".mp4") {
                    spdlog::warn("evslicer {} loadVideoFiles skipped {} (empty/directory/!mp4)", selfId, entry.path().c_str());
                    continue;
                }

                baseName = getBaseName(fname);
                auto ts = videoFileName2Ts(baseName);
                spdlog::info("evslicer {} loadVideoFiles basename: {}, ts: {}", selfId, baseName, ts);

                // check old files
                if(ts - now > hours * 60 * 60) {
                    spdlog::info("evslicer {} file {} old than {} hours: {}, {}", selfId, entry.path().c_str(), hours, ts, now);
                    fs::path fname(this->urlOut + "/" + baseName + ".mp4");
                    fs::remove(fname);
                }
                else {
                    insertTsList(_list, ts, maxSlices);
                }
            }
        }
        catch(exception &e) {
            spdlog::error("evslicer {} {}:{} loadVideoFiles exception : {}",selfId, __FILE__, __LINE__, e.what());
        }
    }

    void insertTsList(set<long> &_list, long elem, int maxSize) {
        // _list.insert(lower_bound(_list.begin(), _list.end(), elem), elem);
        if(_list.size() == 0) {
            _list.insert(_list.begin(),elem);
            return;
        }

        auto itr = _list.rbegin();

        for(; itr != _list.rend(); itr++) {
            if(*itr < elem){
                break;
            }
        }

        if(itr == _list.rbegin() ) {
            _list.insert(_list.end(), elem);
        }else{
            _list.insert(itr.base(), elem);
        }

        if(_list.size() > maxSize) {
            lock_guard<mutex> lg(mutTsList);
            _list.erase(_list.begin());
        }
    }

    // file monitor callback
    static void fileMonHandler(const std::vector<event>& evts, void *pUserData)
    {    
        static string lastFile;
        string ext = ".mp4";
        auto self = static_cast<EvSlicer*>(pUserData);

        for(auto &i : evts) {
            string fullPath = i.get_path();
            size_t pos = fullPath.find(ext, 0);
            if(fullPath.size() < ext.size() ||  pos == string::npos || pos != (fullPath.size() - ext.size())) {
                spdlog::debug("evslicer {} invalid file: {}, last: {}", self->selfId, fullPath, lastFile);
                continue;
            }

            if(lastFile == i.get_path()) {
                spdlog::debug("evslicer {} skip file : {}, last: {}", self->selfId, fullPath, lastFile);
                continue;
            }
            else if(!lastFile.empty()) {
                spdlog::debug("evslicer {} filemon file: {}, ts: {}, last: {}", self->selfId, i.get_path().c_str(), i.get_time(), lastFile);
                try {
                    auto baseName = self->getBaseName(lastFile);
                    auto ts = self->videoFileName2Ts(baseName);
                    if(ts == -1) {
                        spdlog::error("evslicer {} fileMonHandler failed to process file: {}", self->selfId, lastFile);
                    }else{
                        self->insertTsList(self->sTsList, ts, self->numSlices);
                    }
                }
                catch(exception &e) {
                    spdlog::error("evslicer {} fileMonHandler exception: {}", self->selfId, e.what());
                }
            }
            else {
            }

            lastFile = i.get_path();
        }
    }

    // find video files
    vector<string> findSlicesByRange(long tss, long tse, int offsetS, int offsetE)
    {
        vector<string> ret;
        
        lock_guard<mutex> lg(mutTsList);
        if(this->sTsList.size() == 0) {
            return ret;
        }

        long first = *(this->sTsList.begin());
        long end =  *(--(this->sTsList.end()));

        if(tse < first  || tss > end) {
            spdlog::info("evslicer {} event range ({}, {}) is in local range ({}, {}).", selfId, tss, tse, first, end);
            return ret;
        }

        first = end = 0;
        vector<long> tmp;
        int found = 0;
        auto itr = this->sTsList.rbegin();
        for(; itr != this->sTsList.rend(); itr++) {
            if(found == 3) {
                break;
            }
            if(found & 1) {
                tmp.push_back(*itr);
                spdlog::info("\t matched file: {}, s:{}, e:{}", *itr, tse, tss);
            }

            if(tse >= *itr) {
                if((found &1) != 1) {
                    tmp.push_back(*itr);
                    spdlog::info("\t matched file: {}, s:{}, e:{}", *itr, tse, tss);
                    found |= 1;
                }
            }

            if(tss >= *itr) {
                if((found &2) != 2) {
                    tmp.push_back(*itr);
                    spdlog::info("\t matched file: {}, s:{}, e:{}", *itr, tse, tss);
                    found |=2;
                }
            }      
        }

        if(found ==3) {
            string sf;
            auto itr = tmp.rbegin();
            for(; itr != tmp.rend(); itr++) {
                string fname = videoFileTs2Name(*itr, true);
                sf += "\n\t" + this->urlOut + "/" + fname + ".mp4, " + to_string(*itr);
                ret.push_back(fname);
            }
            spdlog::info("evslicer {} event {} - {} files to upload: {}", selfId, videoFileTs2Name(tss), videoFileTs2Name(tse), sf);
        }

        return ret;
    }

public:
    EvSlicer()
    {
        const char *strEnv = getenv("DR_PORT");
        if(strEnv != nullptr) {
            drport = strEnv;
        }

        strEnv = getenv("PEERID");
        if(strEnv != nullptr) {
            selfId = strEnv;
            auto v = strutils::split(selfId, ':');
            if(v.size() != 3||v[1] != "evslicer") {
                spdlog::error("evslicer received invalid gid: {}", selfId);
                exit(1);
            }
            devSn = v[0];
            iid = stoi(v[2]);
        }
        else {
            spdlog::error("evslicer failed to start. no SN set");
            exit(1);
        }

        spdlog::info("evslicer {} boot", selfId);

        //
        string addr = string("tcp://127.0.0.1:") + drport;
        int ret = zmqhelper::setupDealer(&pDaemonCtx, &pDaemon, addr, selfId);
        if(ret != 0) {
            spdlog::error("evslicer {} failed to setup dealer {}", devSn, addr);
            exit(1);
        }

        ret = zmqhelper::recvConfigMsg(pDaemon, config, addr, selfId);
        if(ret != 0) {
            spdlog::error("evslicer {} failed to receive configration message {}", devSn, addr);
        }

        init();

        // thread for msg
        thEdgeMsgHandler = thread([this]() {
            while(true) {
                auto body = z_recv_multiple(pDealer,false);
                if(body.size() == 0) {
                    spdlog::error("evslicer {} failed to receive multiple msg: {}", selfId, zmq_strerror(zmq_errno()));
                    continue;
                }
                // full proto msg received.
                handleEdgeMsg(body);
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

        //
        this->loadVideoFiles(this->urlOut, this->hours, this->numSlices, this->sTsList);
        // thread for slicer maintenace
        thSliceMgr = thread([this]() {
            // get old and active slices
            monitor * m = nullptr;

            CreateDirMon(&m, this->urlOut, ".mp4", vector<string>(), EvSlicer::fileMonHandler, (void *)this);
        });
        thSliceMgr.detach();

        // event thread
        thEventHandler = thread([this] {
            while(true)
            {
                string evt;
                int ret = 0;
                unique_lock<mutex> lk(this->mutEvent);
                this->cvEvent.wait(lk, [this] {return !(this->eventQueue.empty());});

                if(!this->eventQueue.empty()) {
                    evt = this->eventQueue.front();
                    this->eventQueue.pop();
                }

                if(evt.empty()) {
                    continue;
                }

                json jEvt = json::parse(evt);

                if(jEvt["type"] == "event") {
                    auto tss = jEvt["start"].get<long>();
                    auto tse = jEvt["end"].get<long>();
                    long offsetS = 0;
                    long offsetE = 0;
                    // TODO: async

                    if(tss < this->bootTime) {
                        spdlog::warn("evslicer {} should we discard old msg?  {} <  bootTime {}", selfId, evt, this->bootTime);
                    }

                    long first = 0, end = 0;
                    if(this->sTsList.size()!=0 ) {
                        first = *(this->sTsList.begin());
                        end =  *(--(this->sTsList.end()));
                    }

                    if(first == 0||tse < first  || tss > end) {
                        spdlog::error("evslicer {} event range ({}, {}) is in local range ({}, {}).", selfId, tss, tse, first, end);
                        continue;
                    }

                    auto v = findSlicesByRange(tss, tse, offsetS, offsetE);
                    if(v.size() == 0) {
                        spdlog::error("evslicer {} ignore upload videos in range ({}, {}): not found", this->selfId, this->videoFileTs2Name(tss), this->videoFileTs2Name(tse));
                    }
                    else {
                        vector<tuple<string, string> > params= {{"startTime", to_string(tss)},{"endTime", to_string(tse)},{"cameraId", ipcSn}, {"headOffset", to_string(offsetS)},{"tailOffset", to_string(offsetE)}};
                        vector<string> fileNames;
                        string sf;
                        for(auto &i: v) {
                            string fname = this->urlOut + "/" + i + ".mp4";
                            fileNames.push_back(fname);
                            sf+="\tfile\t" + fname + "\n";
                        }

                        spdlog::info("evslicer {} file upload range:({},{}) = ({}, {}), url: {}", selfId, tss, tse, this->videoFileTs2Name(tss), this->videoFileTs2Name(tse), this->videoFileServerApi);
                        // TODO: check result and reschedule it
                        if(netutils::postFiles(std::move(this->videoFileServerApi), std::move(params), std::move(fileNames)) != 0) {
                            spdlog::error("evslicer {} failed to upload files:\n{}", selfId, sf);
                        }
                        else {
                            spdlog::info("evslicer {} successfully uploaded ({}, {}) = ({}, {}) files:\n{}", selfId, tss, tse, this->videoFileTs2Name(tss), this->videoFileTs2Name(tse), sf);
                        }
                    }
                }
                else {
                    spdlog::error("evslicer {} unkown event :{}", this->selfId, evt);
                }
            }
        });

        // thread for uploading slices
        getInputFormat();
        setupStream();
    };
    ~EvSlicer()
    {
        if(pSub != nullptr) {
            zmq_close(pSub);
            pSub = nullptr;
        }
        if(pSubCtx != nullptr) {
            zmq_ctx_destroy(pSubCtx);
            pSubCtx = nullptr;
        }
        if(pDealer != nullptr) {
            zmq_close(pSub);
            pDealer = nullptr;
        }
        if(pDealerCtx != nullptr) {
            zmq_ctx_destroy(pSub);
            pDealerCtx = nullptr;
        }
        freeStream();
    };
};

int main(int argc, const char *argv[])
{
    av_log_set_level(AV_LOG_ERROR);
    spdlog::set_level(spdlog::level::info);
    EvSlicer es;
    es.join();
    return 0;
}