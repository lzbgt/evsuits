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
#define NUM_DAYS_DEFAULT 2
#define MINUTES_PER_SLICE_DEFAULT 1
// 2 days, 5 minutes per record
    void *pSubCtx = nullptr, *pDealerCtx = nullptr; // for packets relay
    void *pSub = nullptr, *pDealer = nullptr, *pDaemonCtx = nullptr, *pDaemon = nullptr;
    string urlOut, urlPub, urlRouter, devSn, mgrSn, selfId, pullerGid;
    int iid, days, minutes, numSlices, segHead = 0;
    bool enablePush = false, bSegFull = false;
    AVFormatContext *pAVFormatRemux = nullptr;
    AVFormatContext *pAVFormatInput = nullptr;
    AVDictionary *pOptsRemux = nullptr;
    int *streamList = nullptr;
    time_t tsLastBoot, tsUpdateTime;
    json config;
    thread thMsgProcessor, thSliceMgr;
    string drport = "5549";
    json slices;
    bool gotFormat = false;
    vector<long> vTsOld;
    mutex mutTsOld;
    vector<long> vTsActive;
    mutex mutTsActive;
    map<long, string> mapTs2BaseName;
    mutex mutTs2BaseName;

    int handleMsg(vector<vector<uint8_t> > v)
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
                    pAVFormatInput = (AVFormatContext *)malloc(sizeof(AVFormatContext));
                    AVFormatCtxSerializer::decode((char *)(v[2].data()), v[2].size(), pAVFormatInput);
                    gotFormat = true;
                    spdlog::info("evslicer {} got avformat from {}", selfId, peerId);
                }
                else {
                    spdlog::info("evslicer {} received msg from {}, type = {}, data = {}", selfId, peerId, meta, data.dump());
                }
            }
            catch(exception &e) {
                spdlog::error("evslicer {} failed to process msg:{}", selfId, msg);
            }
        }
        else {
            spdlog::error("evslicer {} get invalid msg with size {}: {}", selfId, v.size(), msg);
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

            json evpuller = ipc["modules"]["evpuller"][0];
            pullerGid = evpuller["sn"].get<string>() + ":evpuller:" + to_string(evpuller["iid"]);
            mgrSn = evmgr["sn"];

            if(evslicer.count("path") == 0){
                spdlog::info("evslicer {} no params for path, using default: {}", selfId, URLOUT_DEFAULT);
                urlOut = URLOUT_DEFAULT;
            }
            else {
                urlOut = evslicer["path"];
            }

            if(evslicer.count("days") == 0) {
                spdlog::info("evslicer {} no params for days, using default: {}", selfId, NUM_DAYS_DEFAULT);
                days = NUM_DAYS_DEFAULT;
            }
            else {
                days = evslicer["days"].get<int>();
            }

            if(evslicer.count("minutes") == 0) {
                spdlog::info("evslicer {} no params for minutes, using default: {}", selfId, MINUTES_PER_SLICE_DEFAULT);
                minutes = MINUTES_PER_SLICE_DEFAULT;
            }
            else {
                minutes = evslicer["minutes"].get<int>();
            }

            numSlices = 24 * days * 60 /minutes;

            spdlog::info("evslicer mkdir -p {}", selfId, urlOut);
            ret = system((string("mkdir -p ") + urlOut).c_str());
            // if(ret == -1) {
            //     spdlog::error("failed to create {} dir", urlOut);
            //     exit(1);
            // }

            urlPub = string("tcp://") + evpuller["addr"].get<string>() + ":" + to_string(evpuller["port-pub"]);
            urlRouter = string("tcp://") + evmgr["addr"].get<string>() + ":" + to_string(evmgr["port-router"]);
            spdlog::info("evslicer {} will connect to {} for sub, {} for router", selfId, urlPub, urlRouter);

            // setup sub
            pSubCtx = zmq_ctx_new();
            pSub = zmq_socket(pSubCtx, ZMQ_SUB);
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
            ret = zmq_setsockopt(pDealer, ZMQ_IDENTITY, selfId.c_str(), selfId.size());
            ret += zmq_setsockopt (pDealer, ZMQ_ROUTING_ID, selfId.c_str(), selfId.size());
            if(ret < 0) {
                spdlog::error("evpusher {} {} failed setsockopts router: {}", selfId, urlRouter);
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

        uint64_t failedCnt = 0;
        // TODO: change to notification style
        while(!gotFormat) {
            ret = z_send_multiple(pDealer, body);
            if(ret < 0) {
                spdlog::error("evslicer {}, failed to send hello to puller: {}", selfId, zmq_strerror(zmq_errno()));
                continue;
            }
            this_thread::sleep_for(chrono::seconds(20));
        }
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
        av_dict_set(&pOptsRemux, "segment_format", "mp4", 0);
        av_dict_set(&pOptsRemux, "f", "segment", 0);
        av_dict_set(&pOptsRemux, "segment_time", "20", 0);
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
            name = urlOut + "/" + "%06d.mp4";
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
            av_dict_set(&pOptsRemux, "segment_start_number", to_string(segHead+1).data(), 0);
            ret = avformat_write_header(pAVFormatRemux, &pOptsRemux);
            if (ret < 0) {
                spdlog::error("evslicer {} error occurred when opening output file", selfId);
            }

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

    string getBaseName(const string &fname) {
        string ret;
        auto posS = fname.find_last_of('/');
        if(posS == string::npos) {
            posS = 0;
        }else{
            posS = posS +1;
        }
        auto posE = fname.find_last_of('.');
        if(posE == string::npos) {
            posE = fname.size()-1;
        }else{
            posE = posE -1;
        }
        if(posE < posS) {
            spdlog::error("evslicer getBaseName invalid filename");
            return ret;
        }

        //spdlog::info("LoadVideoFiles path {}, s {}, e {}", fname, posS, posE);
        return fname.substr(posS, posE - posS + 1);
    }

    vector<long> LoadVideoFiles(string path, int days, int maxSlices, map<long, string> &ts2fileName, vector<long> &tsNeedUpload)
    {
        vector<long> v;
        // get current timestamp
        list<long> tsRing;
        list<long>tsToProcess;

        auto now = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
        try {
            for (const auto & entry : fs::directory_iterator(path)) {
                if(entry.file_size() == 0 || !entry.is_regular_file()||entry.path().extension() != ".mp4") {
                    spdlog::warn("LoasdVideoFiles skipped {} (empty/directory/!mp4)", entry.path().c_str());
                    continue;
                }

                auto ftime = fs::last_write_time(entry.path());
                auto ts = decltype(ftime)::clock::to_time_t(ftime);

                // check if processed already
                if(ts2fileName.count(ts) != 0) {
                    spdlog::warn("LoasdVideoFiles multiple files with same timestamp: {}, {}(skipped), ", ts2fileName[ts], entry.path().c_str());
                    continue;
                }

                // check old files
                if(ts - now > days * 24 * 60 * 60) {
                    spdlog::info("file {} old that {} days", entry.path().c_str(), days);
                    tsToProcess.insert(std::upper_bound(tsToProcess.begin(), tsToProcess.end(), ts), ts);
                }
                else {
                    tsRing.insert(std::upper_bound(tsRing.begin(), tsRing.end(), ts), ts);
                }

                // add to map
                string fname = entry.path().c_str();
                auto baseName = getBaseName(fname);

                //spdlog::info("LoadVideoFiles path {}, s {}, e {}", fname, posS, posE);
                ts2fileName[ts] = baseName;
            }
        }
        catch(exception &e) {
            spdlog::error("LoasdVideoFiles exception : {}", e.what());
        }

        // skip old items
        list<long>olds;
        int delta = maxSlices - tsRing.size();
        int skip = delta < 0? (-delta):0;
        spdlog::info("LoasdVideoFiles max: {}, current: {}, skip: {}",maxSlices, tsRing.size(), skip);
        int idx = 0;
        list<long>::iterator pos = tsRing.begin();
        for(auto &i:tsRing) {
            if(idx < skip) {
                idx++;
                pos++;
                continue;
            }
            v.push_back(i);
        }
        // merge
        if(skip > 0) {
            tsToProcess.insert(std::upper_bound(tsToProcess.begin(), tsToProcess.end(), tsRing.front()), tsRing.begin(), pos);
        }
        
        //
        for(auto &i:tsToProcess) {
            tsNeedUpload.push_back(i);
        }

        return v;
    }

    // file monitor callback
    static void fileMonHandler(const std::vector<event>& evts, void *pUserData) {
        static string lastFile;
        static long lastTs;

        auto self = static_cast<EvSlicer*>(pUserData);
        for(auto &i : evts) {
            spdlog::info("evslicer {} filemon file: {}, ts: {}", self->selfId, i.get_path().c_str(), i.get_time());
            if(lastFile == i.get_path()) {
                // skip
            }else if(!lastFile.empty()){
                // insert into ts active
                auto lockg = lock_guard(self->mutTsActive);
                if(self->segHead >= self->numSlices) {
                    //wrap it;
                    self->segHead = 0;
                    self->bSegFull = true;
                }

                if(self->bSegFull) {
                    // TODO: backup orignal self->vTsActive[self->segHead]
                }

                try{
                    auto ftime = fs::last_write_time(lastFile);
                    auto baseName = self->getBaseName(lastFile);
                    auto ts = decltype(ftime)::clock::to_time_t(ftime);
                    auto oldTs = self->vTsActive[self->segHead];
                    self->vTsActive[self->segHead] = ts;
                    self->mapTs2BaseName[ts] = baseName;
                    // erase old ts to save memory
                    self->mapTs2BaseName.erase(oldTs);
                    self->segHead++;
                    spdlog::info("evslicer {} fileMonHandler video seg done: {}/{}.mp4, ts:{}", self->selfId, self->urlOut, baseName, ts);
                }catch(exception &e) {
                    spdlog::error("evslicer {} fileMonHandler exception: {}", self->selfId, e.what());
                }
            }else{
                //nop
            }
            lastFile = i.get_path();
        }
    }

    // find video files
    int findTsRang(vector<long> &vTs, long start, long end, int &poss, int &pose) {
        return 0;
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
        thMsgProcessor = thread([this]() {
            while(true) {
                auto body = z_recv_multiple(pDealer,false);
                if(body.size() == 0) {
                    spdlog::error("evslicer {} failed to receive multiple msg: {}", selfId, zmq_strerror(zmq_errno()));
                    continue;
                }
                // full proto msg received.
                handleMsg(body);
            }
        });
        thMsgProcessor.detach();

        // thread for slicer maintenace
        thSliceMgr = thread([this]() {
            // get old and active slices
            this->vTsActive = this->LoadVideoFiles(this->urlOut, this->days, this->numSlices, this->mapTs2BaseName, this->vTsOld);
            this->segHead = this->vTsActive.size();
            monitor * m = nullptr;
            
            CreateDirMon(&m, this->urlOut, ".mp4", vector<string>(), EvSlicer::fileMonHandler, (void *)this);
        });
        thSliceMgr.detach();

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
    av_log_set_level(AV_LOG_INFO);
    spdlog::set_level(spdlog::level::info);
    EvSlicer es;
    es.join();
    return 0;
}