/*
module: evmlmotion
description:
author: Bruce.Lu <lzbgt@icloud.com>
created: 2019/08/23
update: 2019/09/10
*/

#include <stdlib.h>
#include <string>
#include <thread>
#include <iostream>
#include <chrono>
#include <future>
#include <vector>
#include <queue>
#include <atomic>

#include <cstdlib>
#include <ctime>
#include "zmqhelper.hpp"
#include "tinythread.hpp"
#include "common.hpp"
#include "avcvhelpers.hpp"
#include "database.h"

using namespace std;
using namespace zmqhelper;

#define URLOUT_DEFAULT "frames"
#define NUM_PKT_IGNORE 18*5
#define FRAME_SIZE 500

#ifdef DEBUG
// TODO: remove me
cv::Mat matShow1, matShow2, matShow3;
#endif

bool gFirst = true;

struct DetectParam {
    int thre;
    int area;
    int fpsIn;
    int fpsProc;
    int pre;
    int post;
    float entropy;
    int maxDuration; // max event video length in minutes
    cv::Point2f region[2];
};

enum EventState {
    NONE,
    PRE,
    IN,
    POST
};

class EvMLMotion: public TinyThread {
private:
    void *pSubCtx = nullptr, *pDealerCtx = nullptr; // for packets relay
    void *pSub = nullptr, *pDealer = nullptr, *pDaemonCtx = nullptr, *pDaemon = nullptr;
    string urlOut, urlPub, urlRouter, devSn, mgrSn, selfId, pullerGid, slicerGid;
    int iid;
    AVFormatContext *pAVFormatInput = nullptr;
    AVCodecContext *pCodecCtx = nullptr;
    DetectParam detPara = {25, 500, -1, 3, 3, 30, 0.3, 25, {{0,0},{0,0}}};
    EventState evtState = EventState::NONE;
    // chrono::system_clock::time_point packetTm, evtStartTmLast, evtStartTmOrig;
    long long packetTm = 0, evtStartTmLast = 0, evtStartTmOrig = 0, evtStartTmPre = 0;
    queue<string> *evtQueue;
    int streamIdx = -1;
    json config;
    thread thEdgeMsgHandler, thCloudMsgHandler;
    thread thEvent;
    string drport = "5549";
    condition_variable cvMsg;
    mutex mutMsg;
    bool gotFormat = false;
    long long packetTs = 0;
    long long packetTsDelta = 0;
    float pps = 0;
    int pktLag = 0;
    //

    int handleCloudMsg(vector<vector<uint8_t> > v)
    {
        int ret = 0;
        string peerId, metaType, metaValue, msg;
        json data;
        for(auto &b:v) {
            msg +=body2str(b) + ";";
        }

        // msg = msg.substr(0, msg.size()> 100? 15:msg.size());
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
                        spdlog::info("evmlmotion {} received {} cmd from cluster mgr {}", selfId, metaValue, daemonId);
                        bProcessed = true;
                        exit(0);
                    }
                }
            }
            catch(exception &e) {
                spdlog::error("evmlmotion {} exception to process msg {}: {}", selfId, msg, e.what());
            }
        }

        if(!bProcessed) {
            spdlog::error("evmlmotion {} received msg having no implementation from peer: {}", selfId, msg);
        }

        return ret;
    }

    int handleEdgeMsg(vector<vector<uint8_t> > v)
    {
        int ret = 0;
        string peerId, metaType, metaValue, msg;
        json data;
        for(auto &b:v) {
            msg +=body2str(b) + ";";
        }

        msg = msg.substr(0, msg.size()> EV_MSG_DEBUG_LEN? EV_MSG_DEBUG_LEN:msg.size());

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
                string clusterMgrId = this->mgrSn + ":evmgr:0";
                if(peerId == clusterMgrId) {
                    //
                }
                else if(peerId == pullerGid) {
                    if(metaType == EV_MSG_META_AVFORMATCTX) {
                        lock_guard<mutex> lock(this->mutMsg);
                        if(pAVFormatInput == nullptr) {
                            pAVFormatInput = (AVFormatContext *)malloc(sizeof(AVFormatContext));
                            AVFormatCtxSerializer::decode((char *)(v[2].data()), v[2].size(), pAVFormatInput);
                            gotFormat = true;
                            cvMsg.notify_one();
                        }
                        else {
                            spdlog::warn("evmlmotion {} received avformatctx msg from {}, but already proceessed before, restarting", selfId, peerId);
                            spdlog::error("evmlmotion {} restart since reinit", selfId);
                            exit(0);
                        }
                        bProcessed = true;
                    }
                }
            }
            catch(exception &e) {
                spdlog::error("evmlmotion {} exception to process msg {}: {}", selfId, msg, e.what());
            }
        }

        if(!bProcessed) {
            spdlog::error("evmlmotion {} received msg having no implementation from peer: {}", selfId, msg);
        }

        return ret;
    }

    int init()
    {
        int ret = 0;
        bool found = false;
        try {
            spdlog::info("evmlmotion boot config: {} -> {}", selfId, config.dump());
            json evmlmotion;
            json &evmgr = this->config;
            json ipc;
            json ipcs = evmgr["ipcs"];
            for(auto &j: ipcs) {
                json mls = j["modules"]["evml"];
                for(auto &p:mls) {
                    if(p["sn"] == devSn && p["type"] == "motion" && p["iid"] == iid && p["enabled"] != 0) {
                        evmlmotion = p;
                        iid = p["iid"];
                        break;
                    }
                }
                if(evmlmotion.size() != 0) {
                    ipc = j;
                    break;
                }
            }

            if(ipc.size()!=0 && evmlmotion.size()!=0) {
                found = true;
            }

            if(!found) {
                spdlog::error("evmlmotion {}: no valid config found. retrying load config...", devSn);
                exit(1);
            }
            // TODO: currently just take the first puller, but should test connectivity
            json evpuller = ipc["modules"]["evpuller"][0];
            pullerGid = evpuller["sn"].get<string>() + ":evpuller:" + to_string(evpuller["iid"]);
            mgrSn = evmgr["sn"];

            // TODO: connect to the first slicer
            json evslicer = ipc["modules"]["evslicer"][0];
            slicerGid = evslicer["sn"].get<string>()+":evslicer:" + to_string(evslicer["iid"]);

            int portPub = 5556;
            if(evpuller.count("portPub") != 0 && evpuller["portPub"].is_number_integer()) {
                portPub = evpuller["portPub"];
            }
            else if(evpuller.count("port-pub") != 0 && evpuller["port-pub"].is_number_integer()) {
                portPub = evpuller["port-pub"];
            }

            int portRouter = 5550;
            if(evmgr.count("portRouter") != 0 && evmgr["portRouter"].is_number_integer()) {
                portRouter = evmgr["portRouter"];
            }
            else if(evmgr.count("port-router") != 0 && evmgr["port-router"].is_number_integer()) {
                portRouter = evmgr["port-router"];
            }

            urlPub = string("tcp://") + evpuller["addr"].get<string>() + ":" + to_string(portPub);
            urlRouter = string("tcp://") + evmgr["addr"].get<string>() + ":" + to_string(portRouter);
            spdlog::info("evmlmotion {} will connect to {} for sub, {} for router", selfId, urlPub, urlRouter);

            // TODO: multiple protocols support
            if(evmlmotion.count("path") == 0) {
                spdlog::info("evmlmotion {} no params for path, using default: {}", selfId, URLOUT_DEFAULT);
                urlOut = URLOUT_DEFAULT;
            }
            else {
                urlOut = evmlmotion["path"];
            }

            ret = system(("mkdir -p " +urlOut).c_str());
            // if(ret == -1) {
            //     spdlog::error("failed mkdir {}", urlOut);
            //     return -1;
            // }

            // detection params
            if(evmlmotion.count("thresh") == 0||evmlmotion["thresh"] < 10 ||evmlmotion["thresh"] >= 255) {
                spdlog::info("evmlmotion {} invalid thresh value. should be in (10,255), default to {}", selfId, detPara.thre);
            }
            else {
                detPara.thre = evmlmotion["thresh"];
            }

            if(evmlmotion.count("area") == 0||evmlmotion["area"] < 10 ||evmlmotion["area"] >= int(FRAME_SIZE*FRAME_SIZE)*9/10) {
                spdlog::info("evmlmotion {} invalid area value. should be in (10, 500*500*/10), default to {}", selfId, detPara.area);
            }
            else {
                detPara.area = evmlmotion["area"];
            }

            if(evmlmotion.count("pre") == 0||evmlmotion["pre"] < 1 ||evmlmotion["pre"] >= 120) {
                spdlog::info("evmlmotion {} invalid pre value. should be in (1, 120), default to {}", selfId, detPara.pre);
            }
            else {
                detPara.pre = evmlmotion["pre"];
            }

            if(evmlmotion.count("post") == 0||evmlmotion["post"] < 6 ||evmlmotion["post"] >= 120) {
                spdlog::info("evmlmotion {} invalid post value. should be in (6, 120), default to {}", selfId, detPara.post);
            }
            else {
                detPara.post = evmlmotion["post"];
            }

            if(evmlmotion.count("entropy") == 0||evmlmotion["entropy"] < 0 || evmlmotion["entropy"] >= 10) {
                spdlog::info("evmlmotion {} invalid entropy value. should be in (0, 10), default to {}", selfId, detPara.entropy);
            }
            else {
                detPara.entropy = evmlmotion["entropy"];
            }

            if(evmlmotion.count("fpsProc") == 0|| !evmlmotion["fpsProc"].is_number_integer() ||evmlmotion["fpsProc"] < 0 || evmlmotion["fpsProc"] >= 40) {
                spdlog::info("evmlmotion {} invalid fpsProc value. should be in (0, 40) as int, default to {}", selfId, detPara.fpsProc);
            }
            else {
                detPara.fpsProc = evmlmotion["fpsProc"];
            }

            if(evmlmotion.count("maxDuration") == 0|| !evmlmotion["maxDuration"].is_number_integer() ||evmlmotion["maxDuration"] <= 1 || evmlmotion["maxDuration"] >= 100) {
                spdlog::info("evmlmotion {} invalid maxDuration value. should be in (0, 100) as int(minutes), default to {}", selfId, detPara.maxDuration);
            }
            else {
                detPara.maxDuration = evmlmotion["maxDuration"];
            }

            if(evmlmotion.count("region") == 0|| !evmlmotion["maxDuration"].is_object()) {
                json &region = evmlmotion["region"];
                if((region.count("center") == 0|| !region["center"].is_array()) || (region.count("wh") == 0||!region["wh"].is_array())){
                    spdlog::error("evmlmotion {} invalid region config: format. ignored", selfId);
                }else{
                    cv::Point2f p1, p2;
                    const float EVML_MARGIN_F = 0.0001;
                    try{
                        p1.x = region["center"][0];
                        p1.y = region["center"][1];
                        p2.x = region["wh"][0];
                        p2.y = region["wh"][1];
                        if((p1.x - p2.x/2) < EVML_MARGIN_F || (p1.x + p2.x/2) > (1 - EVML_MARGIN_F) || 
                          (p1.y - p2.y/2) < EVML_MARGIN_F || (p1.y + p2.y/2) > (1 - EVML_MARGIN_F)) {
                              spdlog::error("evmlmotion {} invalid region config: valid exceeding. ignored", selfId);
                        }else{
                            detPara.region[0] = p1;
                            detPara.region[1] = p2;
                        }
                    }catch(exception &e) {
                        spdlog::error("evmlmotion {} failed to parse regoin config: {}. ignored", selfId, e.what());
                    }
                }
            }
            else {
                detPara.maxDuration = evmlmotion["maxDuration"];
            }

            spdlog::info("evmlmotion {} detection params: entropy {}, area {}, thresh {}, fpsProc {}", selfId, detPara.entropy, detPara.area, detPara.thre, detPara.fpsProc);

            // setup sub
            pSubCtx = zmq_ctx_new();
            pSub = zmq_socket(pSubCtx, ZMQ_SUB);
            //ZMQ_TCP_KEEPALIVE
            //ZMQ_TCP_KEEPALIVE_IDLE
            //ZMQ_TCP_KEEPALIVE_INTVL
            ret = 1;
            zmq_setsockopt(pSub, ZMQ_TCP_KEEPALIVE, &ret, sizeof (ret));
            ret = 20;
            zmq_setsockopt(pSub, ZMQ_TCP_KEEPALIVE_IDLE, &ret, sizeof (ret));
            zmq_setsockopt(pSub, ZMQ_TCP_KEEPALIVE_INTVL, &ret, sizeof (ret));
            ret = zmq_setsockopt(pSub, ZMQ_SUBSCRIBE, "", 0);
            if(ret != 0) {
                spdlog::error("evmlmotion {} failed set setsockopt: {}", selfId, urlPub);
                exit(1);
            }
            ret = zmq_connect(pSub, urlPub.c_str());
            if(ret != 0) {
                spdlog::error("evmlmotion {} failed connect pub: {}", selfId, urlPub);
                exit(1);
            }

            // setup dealer
            pDealerCtx = zmq_ctx_new();
            pDealer = zmq_socket(pDealerCtx, ZMQ_DEALER);
            spdlog::info("evmlmotion {} connect to router {}", selfId, urlRouter);
            ret = 1;
            zmq_setsockopt(pDealer, ZMQ_TCP_KEEPALIVE, &ret, sizeof (ret));
            ret = 5;
            zmq_setsockopt(pDealer, ZMQ_TCP_KEEPALIVE_IDLE, &ret, sizeof (ret));
            zmq_setsockopt(pDealer, ZMQ_TCP_KEEPALIVE_INTVL, &ret, sizeof (ret));
            ret = zmq_setsockopt(pDealer, ZMQ_IDENTITY, selfId.c_str(), selfId.size());
            ret += zmq_setsockopt (pDealer, ZMQ_ROUTING_ID, selfId.c_str(), selfId.size());
            if(ret < 0) {
                spdlog::error("evmlmotion {} {} failed setsockopts router: {}", selfId, urlRouter);
                exit(1);
            }
            if(ret < 0) {
                spdlog::error("evmlmotion {} failed setsockopts router: {}", selfId, urlRouter);
                exit(1);
            }
            ret = zmq_connect(pDealer, urlRouter.c_str());
            if(ret != 0) {
                spdlog::error("evmlmotion {} failed connect dealer: {}", selfId, urlRouter);
                exit(1);
            }
            //ping
            ret = ping();
        }
        catch(exception &e) {
            spdlog::error("evmlmotion {} exception in EvPuller.init {:s} retrying", selfId, e.what());
            exit(1);
        }
        return 0;
    }

    int ping()
    {
        // send hello to router
        int ret = 0;
        vector<vector<uint8_t> >body;
        // since identity is auto set
        body.push_back(str2body(mgrSn+":evmgr:0"));
        body.push_back(str2body(EV_MSG_META_PING)); // blank meta
        body.push_back(str2body(MSG_HELLO));

        ret = z_send_multiple(pDealer, body);
        if(ret < 0) {
            spdlog::error("evmlmotion {} failed to send multiple: {}", selfId, zmq_strerror(zmq_errno()));
        }
        else {
            spdlog::info("evmlmotion {} sent hello to router: {}", selfId, mgrSn);
        }

        return ret;
    }

    int getInputFormat()
    {
        int ret = 0;
        // req avformatcontext packet
        // send hello to puller
        spdlog::info("evmlmotion {} send hello to puller: {}", selfId, pullerGid);
        vector<vector<uint8_t> > body;
        body.push_back(str2body(pullerGid));
        json meta;
        meta["type"] = EV_MSG_META_AVFORMATCTX;
        body.push_back(str2body(meta.dump()));
        body.push_back(str2body(MSG_HELLO));
        gotFormat = false;
        ret = z_send_multiple(pDealer, body);
        if(ret < 0) {
            spdlog::error("evpusher {} {}, failed to send hello to puller: {}. exiting...", devSn, iid, zmq_strerror(zmq_errno()));
            // TODO: message report to cloud
            exit(1);
        }
        unique_lock<mutex> lk(this->mutMsg);
        bool got = this->cvMsg.wait_for(lk, 30s, [this] {return this->gotFormat;});
        if(got) {

        }
        else {
            // restart
            spdlog::error("evmlmotion {} failed wait for avformatctx for {}s, restart", devSn, 30);
            exit(1);
        }
        return ret;
    }


    int setupStream()
    {
        int ret = 0;
        //  find video
        for (int i = 0; i < pAVFormatInput->nb_streams; i++) {
            AVStream *in_stream = pAVFormatInput->streams[i];
            AVCodecParameters *in_codecpar = in_stream->codecpar;
            if (in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
                continue;
            }
            streamIdx = i;
            break;
        }

        if(streamIdx == -1) {
            spdlog::error("evmlmotion {} no video stream found.", selfId);
            return -1;
        }

        AVStream *pStream = pAVFormatInput->streams[streamIdx];
        detPara.fpsIn = (int)(pStream->r_frame_rate.num/pStream->r_frame_rate.den);
        AVCodec *pCodec = avcodec_find_decoder(pStream->codecpar->codec_id);
        if (pCodec==NULL) {
            spdlog::error("evmlmotion {} ERROR unsupported codec!", selfId);
            return -1;
        }

        pCodecCtx = avcodec_alloc_context3(pCodec);
        if (!pCodecCtx) {
            spdlog::error("evmlmotion {} failed to allocated memory for AVCodecContext", selfId);
            return -1;
        }
        if (avcodec_parameters_to_context(pCodecCtx, pStream->codecpar) < 0) {
            spdlog::error("evmlmotion {} failed to copy codec params to codec context", selfId);
            return -1;
        }

        if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
            spdlog::error("evmlmotion {} failed to open codec through avcodec_open2", selfId);
            return -1;
        }

        return ret;
    }

    void freeStream()
    {
        if(pAVFormatInput != nullptr) {
            AVFormatCtxSerializer::freeCtx(pAVFormatInput);
            pAVFormatInput = nullptr;
        }

        pAVFormatInput = nullptr;
    }

    int decode_packet(bool detect, AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame)
    {
        int response = avcodec_send_packet(pCodecContext, pPacket);
        if (response < 0) {
            spdlog::error("evmlmotion {} Error while sending a packet to the decoder: {}", selfId, av_err2str(response));
            return response;
        }

        while (response >= 0) {
            response = avcodec_receive_frame(pCodecContext, pFrame);
            if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                break;
            }

            if (response < 0) {
                spdlog::error("evmlmotion {} Error while receiving a frame from the decoder: {}", selfId, av_err2str(response));
                return response;
            }
            else {
                spdlog::debug(
                    "Frame {} (type={}, size={} bytes) pts {} key_frame {} [DTS {}]",
                    pCodecContext->frame_number,
                    av_get_picture_type_char(pFrame->pict_type),
                    pFrame->pkt_size,
                    pFrame->pts,
                    pFrame->key_frame,
                    pFrame->coded_picture_number
                );

                static long long called = 0;
                static int factor = 0;
                called++;
                bool proc = true;
                if(this->pps != 0 && factor == 0) {
                    // lock the value
                    factor = int(int(this->pps) / this->detPara.fpsProc); // regulator to 0 if it was set inresonably high
                    if(factor > 8) {
                        factor = 8;
                    }
                }

                if(factor != 0 ) {
                    if(called % factor == 0) {
                        proc = true;
                        //factor = 0; // reset it open to change
                    }
                    else {
                        proc = false;
                    }
                }

                if(!proc) {
                    if(this->pps != 0 && (called %180) == 0) {
                        spdlog::info("evmlmotion {} pps {}, fpsFactor {}, called {}, lag {}, skip processing", this->selfId, this->pps, factor, called, this->pktLag);
                    }
                    // detectMotion(pCodecContext->pix_fmt, pFrame, false);
                }
                else {
                    if((called % (180*4)) == 0) {
                        spdlog::info("evmlmotion {} pps {}, fpsFactor {}, called {}, lag {}", this->selfId, this->pps, factor, called, this->pktLag);
                    }
                    detectMotion(pCodecContext->pix_fmt, pFrame, detect);
                    factor = 0; // refresh to latest value
                }

                break;
            }
        }
        return 0;
    }

    void makeEvent(string evtType, int ts)
    {
        json p;
        p["type"] = EV_MSG_TYPE_AI_MOTION;
        p["gid"] = selfId;
        p["event"] = evtType; //EV_MSG_EVENT_MOTION_END;
        p["ts"] = ts;
        spdlog::info("evmlmotion {} packet ts delta: {}", selfId, packetTsDelta);
        evtQueue->push(p.dump());
        if(evtQueue->size() > MAX_EVENT_QUEUE_SIZE*2) {
            evtQueue->pop();
        }
    }

    void detectMotion(AVPixelFormat format,AVFrame *pFrame, bool detect = true)
    {
        static bool first = true;
        static cv::Mat avg;
        static vector<vector<cv::Point> > cnts;
        cv::Mat origin, gray, thresh;
        avcvhelpers::frame2mat(format, pFrame, origin);
        // check region
        if(detPara.region[0].x == 0) {
            // do nothing
        }else {
            // crop
            auto w = origin.size().width;
            auto h = origin.size().height;
            auto x = (int)(w * (detPara.region[0].x - detPara.region[1].x/2));
            auto y = (int)(h * (detPara.region[0].y - detPara.region[1].y/2));
            w = w * detPara.region[1].x;
            h = h * detPara.region[1].y;
            cv::Rect crop(x,y,w,h);
            origin = origin(crop);
        }
        cv::resize(origin, gray, cv::Size(FRAME_SIZE,FRAME_SIZE));
        cv::cvtColor(gray, thresh, cv::COLOR_BGR2GRAY);
        float fent = avcvhelpers::getEntropy(thresh);
        cv::GaussianBlur(thresh, gray, cv::Size(21, 21), cv::THRESH_BINARY);
        if(first) {
            // avg = cv::Mat::zeros(gray.size(), CV_32FC3);
            avg.release();
            avg = gray.clone();
            first = false;
            return;
        }

#ifdef DEBUG
        matShow3 = gray.clone();
        matShow2 = origin;
#endif
        // packetTm = chrono::system_clock::now();
        packetTm = packetTs;
        // TODO: AVG
        // cv::accumulateWeighted(gray, avg, 0.5);
        cv::absdiff(gray, avg, thresh);
        avg.release();
        avg = gray.clone();

        if(!detect || fent < detPara.entropy) {
            return;
        }

        cv::threshold(thresh, gray, detPara.thre, 255, cv::THRESH_BINARY);
        cv::dilate(gray, thresh, cv::Mat(), cv::Point(-1,-1), 2);

#ifdef DEBUG
        matShow1 = thresh.clone();
#endif

        cv::findContours(thresh, cnts, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        bool hasEvent = false;
        int evtCnt = 0;
        int i = 0;
        for(; i < cnts.size(); i++) {
            if(cv::contourArea(cnts[i]) < detPara.area) {
                // nothing
            }
            else {
                hasEvent = true;
                evtCnt++;

#ifdef DEBUG
                cv::putText(origin, "motion detected", cv::Point(10, 20), cv::FONT_HERSHEY_SIMPLEX, 0.75, cv::Scalar(0,0,255),2);
#endif

                break;
            }
        } //end for

        spdlog::debug("evmlmotion {} contours {} area {}, thresh {} hasEvent {}", selfId, cnts.size(), hasEvent? cv::contourArea(cnts[i]):0, detPara.area, hasEvent);

        // business logic for event
        // auto dura = chrono::duration_cast<chrono::seconds>(packetTm - evtStartTmLast).count();
        if(hasEvent){
          evtStartTmLast = packetTm;
        }
        
        long long dura = 0;
        if(evtStartTmLast != 0) {
            dura = packetTm - evtStartTmLast;
        }

        switch(evtState) {
        case NONE: {
            if(hasEvent) {
                evtState = PRE;
                spdlog::info("{} state: NONE->PRE ({}, {})", selfId, dura, evtCnt);
                evtCnt = 0;
                evtStartTmOrig = packetTm;
            }
            break;
        }
        case PRE: {
            if(hasEvent) {
                if(dura > detPara.pre /*&& evtCnt < detPara.pre*/) {
                    spdlog::debug("state: PRE->PRE ({}, {})", dura, evtCnt);
                    evtState = PRE;
                    evtStartTmOrig = packetTm;
                    evtCnt = 0;
                }
                else if (true/*dura > detPara.pre && evtCnt >= detPara.pre*/) {
                    evtState = IN;
                    json p;
                    spdlog::info("{} state: PRE->IN ({}, {})", selfId, dura, evtCnt);
                    evtCnt = 0;
                    makeEvent(EV_MSG_EVENT_MOTION_START, evtStartTmOrig);
                    auto tmp =  chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
                    packetTsDelta = tmp - packetTm;
                    spdlog::info("{} packet ts delta: {}", selfId, packetTsDelta);
                }
            }
            else {
                if(dura > detPara.pre) {
                    evtState= NONE;
                    spdlog::info("{} state: PRE->NONE ({}, {})", selfId, dura, evtCnt);
                    evtCnt = 0;
                }
            }
            break;
        }
        case IN: {
            if(!hasEvent) {
                if(dura > (int)(detPara.post/2)) {
                    evtState = POST;
                    spdlog::info("{} state: IN->POST ({}, {})", selfId, dura, evtCnt);
                    evtCnt = 0;
                }
            }
            else {
                if( (packetTm - evtStartTmOrig) > (60 *detPara.maxDuration) ) {
                    evtStartTmOrig = packetTm;
                    makeEvent(EV_MSG_EVENT_MOTION_END, packetTs);
                    evtCnt = 0;
                    makeEvent(EV_MSG_EVENT_MOTION_START, packetTs);
                    spdlog::warn("evmlmotion {} event video continued over {} minutes, force segmenting and continue", devSn, detPara.maxDuration);
                }
                spdlog::debug("{} state: IN->IN ({}, {})",selfId, dura, evtCnt);
                evtCnt = 0;
            }
            break;
        }
        case POST: {
            if(!hasEvent) {
                if(dura > detPara.post) {
                    spdlog::info("state: POST->NONE ({}, {})", dura, evtCnt);
                    evtState = NONE;
                    evtCnt = 0;
                    makeEvent(EV_MSG_EVENT_MOTION_END, packetTs);
                }
            }
            else {
                spdlog::info("state: POST->IN ({}, {})", dura, evtCnt);
                evtState=IN;
                evtCnt = 0;
            }
            break;
        }
        }
    }

protected:
    void run()
    {
        bool bStopSig = false;
        uint64_t pktCnt = 0;
        zmq_msg_t msg;
        AVPacket packet;
        json eventToSlicer;

        // eventToSlicer["type"] = "event";
        // eventTOSlicer["extraInfo"] = json(); //array
        // eventToSlicer["start"]
        // eventToSlicer["end"]
        eventToSlicer["sender"] = selfId;

        //event relay thread: motion to slicer and sn:evdaemon:0
        thEvent = thread([&,this]() {
            json meta;
            meta["type"] = EV_MSG_META_EVENT;
            string metaType = meta.dump();
            string daemonId = this->devSn + ":evdaemon:0";
            int ret = 0;
            vector<vector<uint8_t> > v = {str2body(this->slicerGid), str2body(metaType), str2body("")};
            vector<vector<uint8_t> > v1 = {str2body(daemonId), str2body(metaType), str2body("")};
            while(true) {
                if(!this->evtQueue->empty()) {
                    // send to evslicer
                    string evt = this->evtQueue->front();
                    json jevt = json::parse(evt);
                    this->evtQueue->pop();
                    if(jevt["event"] == EV_MSG_EVENT_MOTION_START) {
                        eventToSlicer["type"] = "event";
                        eventToSlicer["start"] = jevt["ts"];
                        eventToSlicer["extraInfo"] = json(); //array
                        eventToSlicer["extraInfo"].push_back(jevt);
                        // TODO: save and load saved evt on crash
                    }
                    else if(jevt["event"] == EV_MSG_EVENT_MOTION_END) {
                        eventToSlicer["end"] = jevt["ts"];
                        eventToSlicer["extraInfo"].push_back(jevt);
                        v[2] = str2body(eventToSlicer.dump());
                        ret = z_send_multiple(this->pDealer, v);
                        if(ret < 0) {
                            spdlog::error("evmlmotion {} failed to send event {} to {}: {}", this->selfId, eventToSlicer.dump(), this->slicerGid, zmq_strerror(zmq_errno()));
                        }
                        else {
                            spdlog::info("evmlmotion {} sent event to {}: {}", this->selfId, this->slicerGid, eventToSlicer.dump());
                        }
                        eventToSlicer.clear();
                    }
                    else {
                        spdlog::error("evmlmotion {} unknown event to {}: {}", this->selfId, this->slicerGid, eventToSlicer.dump());
                    }

                    // send to evdaemon
                    v1[2] = str2body(evt);
                    ret = z_send_multiple(this->pDaemon, v1);
                    if(ret < 0) {
                        spdlog::error("evmlmotion {} failed to send event {} to {}: {}", this->selfId, evt, daemonId, zmq_strerror(zmq_errno()));
                    }
                    else {
                        spdlog::info("evmlmotion {} sent event to {}: {}", this->selfId, daemonId, evt);
                    }

                }
                else {
                    this_thread::sleep_for(chrono::seconds(3));
                }
            }
        });

        thEvent.detach();

        AVFrame *pFrame = av_frame_alloc();
        if (!pFrame) {
            spdlog::error("evmlmotion {} failed to allocated memory for AVFrame", selfId);
            // TODO: message report to cloud
            exit(1);
        }

        auto start = chrono::system_clock::now();
        auto pktCntLast = pktCnt;
        bool bStatsSent = false;
        while(true) {
            if(checkStop() == true) {
                bStopSig = true;
                break;
            }

            // business logic
            int ret =zmq_msg_init(&msg);
            ret = zmq_recvmsg(pSub, &msg, 0);
            if(ret < 0) {
                spdlog::error("evmlmotion {} failed to recv zmq msg: {}", selfId, zmq_strerror(ret));
                continue;
            }
            ret = AVPacketSerializer::decode((char*)zmq_msg_data(&msg), ret, &packet, &packetTs);
            {
                if (ret < 0) {
                    spdlog::error("evmlmotion {} packet decode failed: {}", selfId, ret);
                    continue;
                }
            }
            zmq_msg_close(&msg);
            if(pktCnt % EV_LOG_PACKET_CNT == 0) {
                spdlog::info("evmlmotion {} seq: {}, pts: {}, dts: {}, idx: {}", selfId, pktCnt, packet.pts, packet.dts, packet.stream_index);
            }
            pktCnt++;

            if (packet.stream_index == streamIdx) {
                spdlog::debug("AVPacket.pts {}", packet.pts);
                if(pktCnt < NUM_PKT_IGNORE && gFirst) { // protecting against overflow
                    ret = decode_packet(false, &packet, pCodecCtx, pFrame);
                }
                else {
                    gFirst = false;
                    ret = decode_packet(true, &packet, pCodecCtx, pFrame);
                }
            }

            av_packet_unref(&packet);
            if (ret < 0) {
                // TODO: report message to cloud
                string msg = fmt::format("evmlmotion {} failed to decode packet", selfId);
                json meta;
                json data;
                data["msg"] = msg;
                data["modId"] = selfId;
                data["type"] = EV_MSG_META_TYPE_REPORT;
                data["catId"] = EV_MSG_REPORT_CATID_AVWRITEPIPE;
                data["level"] = EV_MSG_META_VALUE_REPORT_LEVEL_ERROR;
                data["time"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
                data["status"] = "active";
                meta["type"] = EV_MSG_META_TYPE_REPORT;
                meta["value"] = EV_MSG_META_VALUE_REPORT_LEVEL_ERROR;
                z_send(pDaemon, "evcloudsvc", meta.dump(), data.dump());
                spdlog::error(msg);
            }
            else {
                if(!bStatsSent) {
                    bStatsSent = true;
                    string msg = fmt::format("evmlmotion {} successfully decode packet", selfId);
                    json meta;
                    json data;
                    data["msg"] = msg;
                    data["modId"] = selfId;
                    data["type"] = EV_MSG_META_TYPE_REPORT;
                    data["catId"] = EV_MSG_REPORT_CATID_AVWRITEPIPE;
                    data["level"] = EV_MSG_META_VALUE_REPORT_LEVEL_ERROR;
                    data["time"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
                    data["status"] = "recover";
                    meta["type"] = EV_MSG_META_TYPE_REPORT;
                    meta["value"] = EV_MSG_META_VALUE_REPORT_LEVEL_ERROR;
                    z_send(pDaemon, "evcloudsvc", meta.dump(), data.dump());
                    spdlog::info(msg);
                }
            }

            if((pktCnt  - pktCntLast ) == 18) {
                auto now = chrono::system_clock::now();
                auto delta = chrono::duration_cast<chrono::milliseconds>(now - start).count();
                pktLag = chrono::duration_cast<chrono::seconds>(now.time_since_epoch()).count() - this->packetTs;
                this->pps = 18.0 * 1000/delta;
                // if(pktCnt % (180 * 5) == 0) {
                //     spdlog::info("evmlmotion {} metering: 18 packet in {}ms, pps: {}, lag:{}", selfId, delta, pps, pktLag);
                // }

                pktCntLast = pktCnt;
                start = now;
            }
        }

        av_frame_free(&pFrame);
    }
public:
    EvMLMotion() = delete;
    EvMLMotion(queue<string> *queue)
    {
        evtQueue = queue;
        const char *strEnv = getenv("DR_PORT");
        if(strEnv != nullptr) {
            drport = strEnv;
        }

        strEnv = getenv("PEERID");
        if(strEnv != nullptr) {
            selfId = strEnv;
            auto v = strutils::split(selfId, ':');
            if(v.size() != 3||v[1] != "evmlmotion") {
                spdlog::error("evmlmotion received invalid gid: {}", selfId);
                exit(1);
            }
            devSn = v[0];
            iid = stoi(v[2]);
        }
        else {
            spdlog::error("evmlmotion failed to start. no SN set");
            exit(1);
        }

        spdlog::info("evmlmotio {} boot", selfId);

        //
        string addr = string("tcp://127.0.0.1:") + drport;
        int ret = zmqhelper::setupDealer(&pDaemonCtx, &pDaemon, addr, selfId);
        if(ret != 0) {
            spdlog::error("evmlmotion {} failed to setup dealer {}", devSn, addr);
            exit(1);
        }

        ret = zmqhelper::recvConfigMsg(pDaemon, config, addr, selfId);
        if(ret != 0) {
            spdlog::error("evmlmotion {} failed to receive configration message {}", devSn, addr);
        }

        init();

        thCloudMsgHandler = thread([this] {
            while(true)
            {
                auto body = z_recv_multiple(pDaemon,false);
                if(body.size() == 0) {
                    spdlog::error("evslicer {} failed to receive multiple cloud msg: {}", selfId, zmq_strerror(zmq_errno()));
                }
                else {
                    // full proto msg received.
                    this->handleCloudMsg(body);
                }
            }
        });
        thCloudMsgHandler.detach();

        thEdgeMsgHandler = thread([this] {
            while(true)
            {
                auto body = z_recv_multiple(pDealer,false);
                if(body.size() == 0) {
                    spdlog::error("evslicer {} failed to receive multiple edge msg: {}", selfId, zmq_strerror(zmq_errno()));
                }
                else {
                    // full proto msg received.
                    this->handleEdgeMsg(body);
                }
            }
        });
        thEdgeMsgHandler.detach();

        getInputFormat();
        setupStream();
    };
    ~EvMLMotion()
    {
        if(pSub != nullptr) {
            int i = 0;
            zmq_setsockopt(pSub, ZMQ_LINGER, &i, sizeof(i));
            zmq_close(pSub);
            pSub = nullptr;
        }
        if(pSubCtx != nullptr) {
            zmq_ctx_destroy(pSubCtx);
            pSubCtx = nullptr;
        }
        if(pDealer != nullptr) {
            int i = 0;
            zmq_setsockopt(pDealer, ZMQ_LINGER, &i, sizeof(i));
            zmq_close(pDealer);
            pDealer = nullptr;
        }
        if(pDealerCtx != nullptr) {
            zmq_ctx_destroy(pSub);
            pDealerCtx = nullptr;
        }

        if(pDaemon != nullptr) {
            int i = 0;
            zmq_setsockopt(pDaemon, ZMQ_LINGER, &i, sizeof(i));
            zmq_close(pDaemon);
            pDaemon = nullptr;
        }

        if(pDaemonCtx != nullptr) {
            zmq_ctx_destroy(pDaemonCtx);
            pDaemonCtx = nullptr;
        }
    };
};

int main(int argc, const char *argv[])
{
    spdlog::set_level(spdlog::level::info);
    av_log_set_level(AV_LOG_ERROR);
    queue<string> evtQueue;
    EvMLMotion es(&evtQueue);

#ifdef DEBUG
    cv::namedWindow( "Display window", cv::WINDOW_AUTOSIZE );
    while(true) {
        if(gFirst) {
            this_thread::sleep_for(chrono::seconds(5));
            continue;
        }
        cv::imshow("evmlmotion1", matShow1);
        cv::imshow("evmlmotion2", matShow2);
        cv::imshow("evmlmotion3", matShow3);
        if(cv::waitKey(200) == 27) {
            break;
        }
    }
#else
    es.join();
#endif
    return 0;
}