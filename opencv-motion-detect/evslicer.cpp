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

class EvSlicer: public TinyThread {
private:
#define URLOUT_DEFAULT "slices"
#define NUM_DAYS_DEFAULT 2
#define MINUTES_PER_SLICE_DEFAULT 10
// 2 days, 10 minutes per record
#define NUM_SLICES_DEFAULT (24 * NUM_DAYS_DEFAULT * 60 / MINUTES_PER_SLICE_DEFAULT)

    void *pSubCtx = NULL, *pReqCtx = NULL; // for packets relay
    void *pSub = NULL, *pReq = NULL;
    string urlOut, urlPub, urlRep, sn;
    int iid, ;
    bool enablePush = false;
    int *streamList = NULL;
    AVFormatContext *pAVFormatRemux = NULL;
    AVFormatContext *pAVFormatInput = NULL;

    int init()
    {
        bool inited = false;
        // TODO: read db to get sn
        sn = "ILS-3";
        iid = 3;
        while(!inited) {
            // req config
            json jr = cloudutils::registry(sn.c_str(), "evpusher", iid);
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
                spdlog::info("evslicer {} {} will connect to {} for sub, {} for req", sn, iid, urlPub, urlRep);

                data = jr["data"]["services"]["evslicer"];
                for(auto &j: data) {
                    if(j["sn"] == sn && iid == j["iid"] && j["enabled"] != 0) {
                        try{
                            j.at("path").get_to(urlOut);
                        }catch(exception &e) {
                            spdlog::warn("evslicer {} {} exception get PATH for store file: {}", sn, iid, e.what());
                        }
                        
                        break;
                    }
                }
            }
            catch(exception &e) {
                bcnt = true;
                spdlog::error("evpusher {} {} exception in EvPuller.init {:s},  retrying...", sn, iid, e.what());
            }
            if(bcnt || urlOut.empty()) {
                // TODO: waiting for command
                spdlog::warn("evpusher {} {} waiting for command", sn, iid);
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
            spdlog::error("evpusher failed connect to pub: {}, {}", sn, iid);
            return -1;
        }
        ret = zmq_connect(pSub, urlPub.c_str());
        if(ret != 0) {
            spdlog::error("evpusher {} {} failed create sub", sn, iid);
            return -2;
        }

        // setup req
        pReqCtx = zmq_ctx_new();
        pReq = zmq_socket(pReqCtx, ZMQ_REQ);
        spdlog::info("evpusher {} {} try create req to {}", sn, iid, urlRep);
        ret = zmq_connect(pReq, urlRep.c_str());

        if(ret != 0) {
            spdlog::error("evpusher {} {} failed create req to {}", sn, iid, urlRep);
            return -3;
        }

        spdlog::info("evpusher {} {} success setupMq", sn, iid);

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
        AVDictionary *pOptsRemux = NULL;

        // req avformatcontext packet
        // send first packet to init connection
        zmq_msg_t msg;
        zmq_send(pReq, "hello", 5, 0);
        spdlog::info("evpusher {} {} success send hello", sn, iid);
        ret =zmq_msg_init(&msg);
        if(ret != 0) {
            spdlog::error("failed to init zmq msg");
            exit(1);
        }
        // receive packet
        ret = zmq_recvmsg(pReq, &msg, 0);
        spdlog::info("evpusher {} {} recv", sn, iid);
        if(ret < 0) {
            spdlog::error("evpusher {} {} failed to recv zmq msg: {}", sn, iid, zmq_strerror(ret));
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

        ret = avformat_alloc_output_context2(&pAVFormatRemux, NULL, "mp4", urlOut.c_str());
        if (ret < 0) {
            spdlog::error("evpusher {} {} failed create avformatcontext for output: %s", sn, iid, av_err2str(ret));
            exit(1);
        }

        streamList = (int *)av_mallocz_array(pAVFormatInput->nb_streams, sizeof(*streamList));
        spdlog::info("evpusher {} {} numStreams: {:d}", sn, iid, pAVFormatInput->nb_streams);
        if (!streamList) {
            ret = AVERROR(ENOMEM);
            spdlog::error("evpusher {} {} failed create avformatcontext for output: %s", sn, iid, av_err2str(AVERROR(ENOMEM)));
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
                spdlog::error("evpusher {} {} failed allocating output stream", sn, iid);
                ret = AVERROR_UNKNOWN;

            }
            ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
            spdlog::info("evpusher {} {}  copied codepar", sn, iid);
            if (ret < 0) {
                spdlog::error("evpusher {} {}  failed to copy codec parameters", sn, iid);
            }
        }

        for(int i = 0; i < pAVFormatInput->nb_streams; i++ ) {
            spdlog::info("streamList[{:d}]: {:d}", i, streamList[i]);
        }

        av_dump_format(pAVFormatRemux, 0, urlOut.c_str(), 1);

        if (!(pAVFormatRemux->oformat->flags & AVFMT_NOFILE)) {
            spdlog::error("evpusher {} {} failed allocating output stream", sn,iid);
            ret = avio_open2(&pAVFormatRemux->pb, urlOut.c_str(), AVIO_FLAG_WRITE, NULL, &pOptsRemux);
            if (ret < 0) {
                spdlog::error("evpusher {} {} could not open output file '%s'", sn, iid, urlOut);
                exit(1);
            }
        }

        // rtsp tcp
        if(av_dict_set(&pOptsRemux, "rtsp_transport", "tcp", 0) < 0) {
            spdlog::error("evpusher {} {} failed set output pOptsRemux", sn, iid);
            ret = AVERROR_UNKNOWN;
        }

        ret = avformat_write_header(pAVFormatRemux, &pOptsRemux);
        if (ret < 0) {
            spdlog::error("evpusher {} {} error occurred when opening output file", sn, iid);
        }

        return ret;
    }
protected:
    void run()
    {
        bool bStopSig = false;
        while (true) {
            if(checkStop() == true) {
                bStopSig = true;
                break;
            }
            // business logic
            this_thread::sleep_for(chrono::seconds(5));
        }
    }
public:
    EvSlicer() {
        init();
        setupMq();
        setupStream();
    };
    ~EvSlicer() {};
};

int main(int argc, const char *argv[])
{
    spdlog::set_level(spdlog::level::debug);
    EvSlicer es;
    es.join();
    return 0;
}