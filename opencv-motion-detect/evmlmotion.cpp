#pragma GCC diagnostic ignored "-Wunused-private-field"
#pragma GCC diagnostic ignored "-Wunused-variable"

#include <stdlib.h>
#include <string>
#include <thread>
#include <iostream>
#include <chrono>
#include <future>
#include <vector>
#include <queue>

#ifdef OS_LINUX
#include <filesystem>
namespace fs = std::filesystem;
#endif
#include <cstdlib>
#include "zmqhelper.hpp"
#include "tinythread.hpp"
#include "common.hpp"
#include "avcvhelpers.hpp"
#include "database.h"

using namespace std;
using namespace zmqhelper;

#define URLOUT_DEFAULT "frames"
#define NUM_PKT_IGNORE 18*2

#define DEBUG

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
};

enum EventState {
    NONE,
    PRE,
    IN,
    POST
};

class EvMLMotion: public TinyThread {
private:
    void *pSubCtx = NULL, *pDealerCtx = NULL; // for packets relay
    void *pSub = NULL, *pDealer = NULL;
    string urlOut, urlPub, urlRouter, devSn, mgrSn, selfId, pullerGid;
    int iid;
    AVFormatContext *pAVFormatInput = NULL;
    AVCodecContext *pCodecCtx = NULL;
    AVDictionary *pOptsRemux = NULL;
    DetectParam detPara = {25,200,-1,10,3,30};
    EventState evtState = EventState::NONE;
    chrono::system_clock::time_point evtStartTm, evtStartTmLast;
    queue<string> *evtQueue;
    int streamIdx = -1;
    json config;
    thread thPing;
    thread thEvent;
    //

    int init()
    {
        int ret = 0;
        bool inited = false;
        // TODO: read db to get devSn
        devSn = "ILSEVMLMOTION1";
        iid = 1;
        selfId = devSn + ":evmlmotion:" + to_string(iid);
        while(!inited) {
            // TODO: req config
            bool found = false;
            try {
                config = json::parse(cloudutils::config);
                spdlog::info("config: {:s}", config.dump());
                json evmlmotion;
                json evmgr;
                json ipc;

                json data = config["data"];
                for (auto& [key, value] : data.items()) {
                    //std::cout << key << " : " << dynamic_cast<json&>(value).dump() << "\n";
                    evmgr = value;
                    json ipcs = evmgr["ipcs"];
                    for(auto &j: ipcs) {
                        json mls = j["modules"]["evml"];
                        for(auto &p:mls) {
                            if(p["sn"] == devSn && p["iid"] == iid && p["type"] == "motion") {
                                evmlmotion = p;
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
                        break;
                    }
                }

                if(!found) {
                    spdlog::error("evmlmotion {} {}: no valid config found. retrying load config...", devSn, iid);
                    this_thread::sleep_for(chrono::seconds(3));
                    continue;
                }

                // TODO: currently just take the first puller, but should test connectivity
                json evpuller = ipc["modules"]["evpuller"][0];
                pullerGid = evpuller["sn"].get<string>() + ":evpuller:" + to_string(evpuller["iid"]);
                mgrSn = evmgr["sn"];

                urlPub = string("tcp://") + evpuller["addr"].get<string>() + ":" + to_string(evpuller["port-pub"]);
                urlRouter = string("tcp://") + evmgr["addr"].get<string>() + ":" + to_string(evmgr["port-router"]);
                spdlog::info("evmlmotion {} {} will connect to {} for sub, {} for router", devSn, iid, urlPub, urlRouter);
                // TODO: multiple protocols support
                if(evmlmotion.count("path") == 0){
                    spdlog::warn("evslicer {} {} no params for path, using default: {}", devSn, iid, URLOUT_DEFAULT);
                    urlOut = URLOUT_DEFAULT;
                }else{
                    urlOut = evmlmotion["path"];
                }

                ret = system(("mkdir -p " +urlOut).c_str());
                if(ret == -1) {
                    spdlog::error("failed mkdir {}", urlOut);
                    return -1;
                }
            }
            catch(exception &e) {
                spdlog::error("evmlmotion {} {} exception in EvPuller.init {:s} retrying", devSn, iid, e.what());
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
            spdlog::error("evmlmotion {} {} failed to send multiple: {}", devSn, iid, zmq_strerror(zmq_errno()));
            //TODO:
        }
        else {
            spdlog::info("evmlmotion {} {} sent hello to router: {}", devSn, iid, mgrSn);
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
            spdlog::error("evmlmotion {} {} failed set setsockopt: {}", devSn, iid, urlPub);
            return -1;
        }
        ret = zmq_connect(pSub, urlPub.c_str());
        if(ret != 0) {
            spdlog::error("evmlmotion {} {} failed connect pub: {}", devSn, iid, urlPub);
            return -2;
        }

        // setup dealer
        pDealerCtx = zmq_ctx_new();
        pDealer = zmq_socket(pDealerCtx, ZMQ_DEALER);
        spdlog::info("evmlmotion {} {} try create req to {}", devSn, iid, urlRouter);
        ret = zmq_setsockopt(pDealer, ZMQ_IDENTITY, selfId.c_str(), selfId.size());
        if(ret < 0) {
            spdlog::error("evmlmotion {} {} failed setsockopts router: {}", devSn, iid, urlRouter);
            return -3;
        }
        ret = zmq_connect(pDealer, urlRouter.c_str());
        if(ret != 0) {
            spdlog::error("evmlmotion {} {} failed connect dealer: {}", devSn, iid, urlRouter);
            return -4;
        }
        //ping
        ret = ping();
        thPing = thread([&,this]() {
            while(true) {
                this_thread::sleep_for(chrono::seconds(EV_HEARTBEAT_SECONDS-2));
                ping();
            }
        });

        thPing.detach();

        return ret;
    }

    int getInputFormat()
    {
        int ret = 0;
        // req avformatcontext packet
        // send hello to puller
        spdlog::info("evmlmotion {} {} send hello to puller: {}", devSn, iid, pullerGid);
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
                spdlog::error("evmlmotion {} {}, failed to send hello to puller: {}", devSn, iid, zmq_strerror(zmq_errno()));
                continue;
            }

            // expect response with avformatctx
            auto v = z_recv_multiple(pDealer);
            if(v.size() != 3) {
                ret = zmq_errno();
                if(ret != 0) {
                    if(failedCnt % 100 == 0) {
                        spdlog::error("evmlmotion {} {}, error receive avformatctx: {}, {}", devSn, iid, v.size(), zmq_strerror(ret));
                        spdlog::info("evmlmotion {} {} retry connect to peers", devSn, iid);
                    }
                    this_thread::sleep_for(chrono::seconds(5));
                    failedCnt++;
                }
                else {
                    spdlog::error("evmlmotion {} {}, received bad size zmq msg for avformatctx: {}", devSn, iid, v.size());
                }
            }
            else if(body2str(v[0]) != pullerGid) {
                spdlog::error("evmlmotion {} {}, invalid sender for avformatctx: {}, should be: {}", devSn, iid, body2str(v[0]), pullerGid);
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
                    spdlog::error("evmlmotion {} {}, exception in parsing avformatctx packet: {}", devSn, iid, e.what());
                }
            }
        }
        return ret;
    }


    int setupStream()
    {
        int ret = 0;
        //  find video
        for (int i = 0; i < pAVFormatInput->nb_streams; i++) {
            AVStream *out_stream;
            AVStream *in_stream = pAVFormatInput->streams[i];
            AVCodecParameters *in_codecpar = in_stream->codecpar;
            if (in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
                continue;
            }
            streamIdx = i;
            break;
        }

        if(streamIdx == -1) {
            spdlog::error("no video stream found.");
            return -1;
        }

        AVStream *pStream = pAVFormatInput->streams[streamIdx];
        detPara.fpsIn = (int)(pStream->r_frame_rate.num/pStream->r_frame_rate.den);
        AVCodec *pCodec = avcodec_find_decoder(pStream->codecpar->codec_id);
        if (pCodec==NULL) {
            spdlog::error("ERROR unsupported codec!");
            return -1;
        }

        pCodecCtx = avcodec_alloc_context3(pCodec);
        if (!pCodecCtx) {
            spdlog::error("failed to allocated memory for AVCodecContext");
            return -1;
        }
        if (avcodec_parameters_to_context(pCodecCtx, pStream->codecpar) < 0) {
            spdlog::error("failed to copy codec params to codec context");
            return -1;
        }

        if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
            spdlog::error("failed to open codec through avcodec_open2");
            return -1;
        }

        return ret;
    }

    void freeStream()
    {
        if(pAVFormatInput != NULL) {
            AVFormatCtxSerializer::freeCtx(pAVFormatInput);
            pAVFormatInput = NULL;
        }

        pAVFormatInput = NULL;
    }

    int decode_packet(bool detect, AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame)
    {
        int response = avcodec_send_packet(pCodecContext, pPacket);
        if (response < 0) {
            spdlog::error("Error while sending a packet to the decoder: {}", av_err2str(response));
            return response;
        }

        while (response >= 0) {
            response = avcodec_receive_frame(pCodecContext, pFrame);
            if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                break;
            }

            if (response < 0) {
                spdlog::error("Error while receiving a frame from the decoder: {}", av_err2str(response));
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
                // string name = urlOut + "/"+ to_string(chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count()) + ".pgm";
                if(detect) {
                    detectMotion(pCodecContext->pix_fmt,pFrame);
                }
            }
        }
        return 0;
    }

    void detectMotion(AVPixelFormat format,AVFrame *pFrame)
    {
        static bool first = true;
        static cv::Mat avg;
        static vector<vector<cv::Point> > cnts;
        cv::Mat origin, gray, thresh;
        avcvhelpers::frame2mat(format, pFrame, origin);
        cv::resize(origin, gray, cv::Size(500,500));
        cv::cvtColor(gray, thresh, cv::COLOR_BGR2GRAY);
        cv::GaussianBlur(thresh, gray, cv::Size(21, 21), cv::THRESH_BINARY);
        if(first) {
            // avg = cv::Mat::zeros(gray.size(), CV_32FC3);
            avg = gray.clone();
            first = false;
            return;
        }
#ifdef DEBUG
        matShow3 = gray.clone();
#endif
        evtStartTm = chrono::system_clock::now();
        // TODO: AVG
        // cv::accumulateWeighted(gray, avg, 0.5);
        cv::absdiff(gray, avg, thresh);

#ifdef DEBUG
        avg = gray.clone();
#endif

        // TODO:
        cv::threshold(thresh, gray, 25, 255, cv::THRESH_BINARY);
        cv::dilate(gray, thresh, cv::Mat(), cv::Point(-1,-1), 2);

#ifdef DEBUG
        matShow1 = thresh.clone();
#endif

        cv::findContours(thresh, cnts, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        bool hasEvent = false;
        for(int i =0; i < cnts.size(); i++) {
            // TODO:
            if(cv::contourArea(cnts[i]) < 200) {
                // nothing
            }
            else {
                hasEvent = true;
#ifdef DEBUG
                cv::putText(origin, "motion detected", cv::Point(10, 20), cv::FONT_HERSHEY_SIMPLEX, 0.75, cv::Scalar(0,0,255),2);
#endif
                break;
            }
        } //end for
#ifdef DEBUG
        matShow2 = origin;
#endif
        // business logic for event
        auto dura = chrono::duration_cast<chrono::seconds>(evtStartTm - evtStartTmLast).count();
        switch(evtState) {
            case NONE: {
                if(hasEvent) {
                    evtState = PRE;
                    spdlog::info("state: NONE->PRE");
                    evtStartTmLast = evtStartTm;
                }
                break;
            }
            case PRE: {
                if(hasEvent) {
                    if(dura > detPara.pre) {
                        spdlog::info("state: PRE->PRE");
                        evtState = PRE;
                    }else{
                        evtState = IN;
                        json p;
                        spdlog::info("state: PRE->IN");
                        p["type"] = "motion";
                        p["gid"] = selfId;
                        p["event"] = "end";
                        p["ts"] = chrono::duration_cast<chrono::seconds>(evtStartTmLast.time_since_epoch()).count();
                        //p["frame"] = origin.clone();
                        evtQueue->push(p.dump());
                        if(evtQueue->size() > MAX_EVENT_QUEUE_SIZE * 2) {
                            evtQueue->pop();
                        }
                    }
                }else{
                    if(dura > detPara.pre){
                        evtState= NONE;
                        spdlog::info("state: PRE->NONE");
                    }
                }
                break;
            }
            case IN: {
                if(!hasEvent){
                    if(dura > (int)(detPara.post/2)){
                        evtState = POST;
                        spdlog::info("state: IN->POST");
                    }
                }else{
                    evtStartTmLast = evtStartTm;
                    spdlog::info("state: IN->IN");
                }
                break;
            }
            case POST: {
                if(!hasEvent) {
                    if(dura > detPara.post) {
                        spdlog::info("state: POST->NONE");
                        evtState = NONE;
                        json p;
                        p["type"] = "motion";
                        p["gid"] = selfId;
                        p["event"] = "end";
                        p["ts"] = chrono::duration_cast<chrono::seconds>(evtStartTmLast.time_since_epoch()).count() + (int)(detPara.post/2);
                        evtQueue->push(p.dump());
                        if(evtQueue->size() > MAX_EVENT_QUEUE_SIZE*2) {
                            evtQueue->pop();
                        }
                    }
                }else{
                    spdlog::info("state: POST->IN");
                    evtState=IN;
                    evtStartTmLast = evtStartTm;
                }
                break;
            }
        }
    }

protected:
    void run()
    {
        bool bStopSig = false;
        int ret = 0;
        int idx = 0;
        uint64_t pktCnt = 0;
        zmq_msg_t msg;
        AVPacket packet;

        //event thread
        thEvent = thread([&,this](){
            json meta; meta["type"] = EV_MSG_META_EVENT;
            string metaType = meta.dump();
            int ret = 0;
            vector<vector<uint8_t> > v = {str2body(this->pullerGid), str2body(metaType), str2body("")};
            while(true){
                if(!this->evtQueue->empty()){
                    string evt = this->evtQueue->front();
                    v[2] = str2body(evt);
                    this->evtQueue->pop();
                    ret = z_send_multiple(this->pDealer, v);
                    spdlog::info("evmlmotion {} {} send event: {}", this->devSn, this->iid, evt);
                    if(ret < 0) {
                        spdlog::error("evmlmotion {} {} failed to send event: {}, {}", this->devSn, this->iid, evt, zmq_strerror(zmq_errno()));
                    }
                }else{
                    this_thread::sleep_for(chrono::seconds(3));
                }
            }
        });

        thEvent.detach();

        AVFrame *pFrame = av_frame_alloc();
        if (!pFrame) {
            spdlog::error("failed to allocated memory for AVFrame");
            exit(1);
        }
        while(true) {
            auto start = chrono::system_clock::now();
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
            if(pktCnt % 1024 == 0) {
                spdlog::info("seq: {}, pts: {}, dts: {}, dur: {}, idx: {}", pktCnt, packet.pts, packet.dts, packet.duration, packet.stream_index);
            }
            pktCnt++;

            if (packet.stream_index == streamIdx) {
                spdlog::debug("AVPacket.pts {}", packet.pts);
                if(pktCnt < NUM_PKT_IGNORE && gFirst) {
                    ret = decode_packet(false, &packet, pCodecCtx, pFrame);
                }
                else {
                    gFirst = false;
                    ret = decode_packet(true, &packet, pCodecCtx, pFrame);
                }
            }

            av_packet_unref(&packet);
            if (ret < 0) {
                spdlog::error("error muxing packet");
            }
        }

        av_frame_free(&pFrame);
    }
public:
    EvMLMotion() = delete;
    EvMLMotion(queue<string> *queue)
    {
        evtQueue = queue;
        init();
        setupMq();
        getInputFormat();
        setupStream();
    };
    ~EvMLMotion() {
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
    };
};

int main(int argc, const char *argv[])
{
    spdlog::set_level(spdlog::level::info);
    av_log_set_level(AV_LOG_ERROR);
    queue<string> evtQueue;
    EvMLMotion es(&evtQueue);
    es.detach();

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
    while(true) {
        if(evtQueue.size() >  0) {
            string p = evtQueue.front();
            spdlog::info("event: {}", p);
            evtQueue.pop();
        }else{
            this_thread::sleep_for(chrono::duration(chrono::seconds(2)));
        }
    }

#endif
    return 0;
}