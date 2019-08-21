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

int mqErrorMsg(string cls, string devSn, int iid, string extraInfo, int ret)
{
    if(ret < 0) {
        spdlog::error("{} {} {}, {}: {} ", cls, devSn, iid, extraInfo, zmq_strerror(zmq_errno()));
    }

    return ret;
}

class RepSrv: public TinyThread {
private:
    string mgrSn;
    string devSn;
    int iid;
    string urlRep;
    const char * bytes;
    int len;
    void *pDealer=NULL;
    // void *pRepCtx = NULL; // for packets REP
    // void *pRep = NULL;

    int init()
    {
        // int ret = 0;
        // pRepCtx = zmq_ctx_new();
        // pRep = zmq_socket(pRepCtx, ZMQ_REP);
        // ret = zmq_bind(pRep, urlRep.c_str());
        // if(ret < 0) {
        //     spdlog::error("failed to bind rep: {}, {}", zmq_strerror(ret), urlRep.c_str());
        //     this_thread::sleep_for(chrono::seconds(20));
        //     return -1;
        // }
        return 0;
    }
protected:
    void run()
    {
        bool bStopSig = false;

        zmq_msg_t msg;
        zmq_msg_t msg1;
        int ret =zmq_msg_init(&msg);
        zmq_msg_init_data(&msg, (void*)bytes, len, NULL, NULL);
        // declare ready to router
        vector<string>body;
        body.push_back(mgrSn);
        body.push_back("hello");
        int cnt = 0;
        for(auto &i:body) {
            zmq_msg_init(&msg1);
            zmq_msg_init_data(&msg1, (void*)i.c_str(), i.size(), NULL, NULL);
            mqErrorMsg("evpuller", devSn,iid, "failed to send zmq msg", zmq_send_const(pDealer, zmq_msg_data(&msg1), i.size(), cnt==(body.size()-1)?0:ZMQ_SNDMORE));
            zmq_msg_close(&msg1);
            cnt++;
        }

        while (true) {
            if(checkStop() == true) {
                bStopSig = true;
                break;
            }
            spdlog::info("evpuller reqSrv {} {} waiting for req", devSn, iid);
            int ret =zmq_msg_init(&msg1);
            ret = zmq_recvmsg(pDealer, &msg1, 0);
            if(ret < 0) {
                spdlog::error("failed to recv zmq msg: {}", zmq_strerror(ret));
                continue;
            }
            zmq_msg_close(&msg1);
            spdlog::info("evpuller {} {} reveived req", devSn, iid);
            zmq_send_const(pDealer, zmq_msg_data(&msg), len, 0);
        }
    }
public:
    RepSrv() = delete;
    RepSrv(RepSrv &) = delete;
    RepSrv(RepSrv&&) = delete;
    RepSrv(string mgrSn, string devSn, int iid, const char* formatBytes, int len, void *pDealer):mgrSn(mgrSn),devSn(devSn), iid(iid), bytes(formatBytes), len(len), pDealer(pDealer)
    {
        init();
    };
    ~RepSrv()
    {
        // if(pRep != NULL) {
        //     zmq_close(pRep);
        // }
        // if(pRepCtx != NULL) {
        //     zmq_ctx_destroy(pRepCtx);
        // }
    };
};

class EvPuller: public TinyThread {
private:
    void *pPubCtx = NULL; // for packets publishing
    void *pPub = NULL;
    void *pDealerCtx = NULL;
    void *pDealer = NULL;
    AVFormatContext *pAVFormatInput = NULL;
    string urlIn, urlPub, urlDealer, mgrSn, devSn;
    int *streamList = NULL, numStreams = 0, iid;
    json config;

    int init()
    {
        bool inited = false;
        // TODO: load devSn iid from database
        devSn = "ILSEVPULLER1";
        iid = 1;
        int ret = 0;
        while(!inited) {
            // TODO: req config
            bool found = false;
            try {
                config = json::parse(cloudutils::config);
                spdlog::info("config dump: {:s}", config.dump());
                json data = config["data"];
                // first try to check mgr with same sn
                json evpuller;
                json evmgr;
                json ipc;

                for (auto& [key, value] : data.items()) {
                    //std::cout << key << " : " << dynamic_cast<json&>(value).dump() << "\n";
                    evmgr = value;
                    json ipcs = evmgr["ipcs"];
                    for(auto &j: ipcs) {
                        json pullers = j["modules"]["evpuller"];
                        for(auto &p:pullers) {
                            if(p["sn"] == devSn && p["iid"] == iid) {
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
                        break;
                    }
                }

                if(!found) {
                    this_thread::sleep_for(chrono::seconds(3));
                    spdlog::error("evpuller {} {} no valid config found. retrying load config...", devSn, iid);
                    continue;
                }

                mgrSn = evmgr["sn"];
                string user = ipc["user"];
                string passwd = ipc["password"];
                urlIn = "rtsp://" + user + ":" + passwd + "@" + ipc["addr"].get<string>() + "/h264/ch1/sub/av_stream";
                urlPub = string("tcp://") + evpuller["addr"].get<string>() + ":" + to_string(evpuller["port-pub"]);
                // urlRep = string("tcp://") +data["addr"].get<string>() + ":" + to_string(data["port-rep"]);
                urlDealer = "tcp://" + evmgr["addr"].get<string>() + string(":") + to_string(evmgr["port-router"]);
                spdlog::info("evpuller {} {} bind on {} for pub, {} for dealer", devSn, iid, urlPub, urlDealer);

                pPubCtx = zmq_ctx_new();
                pPub = zmq_socket(pPubCtx, ZMQ_PUB);
                ret = mqErrorMsg("evpuller", devSn, iid, "failed to bind zmq", zmq_bind(pPub, urlPub.c_str()));
                pDealerCtx = zmq_ctx_new();
                pDealer = zmq_socket(pDealerCtx, ZMQ_DEALER);
                string ident = devSn+":evpuller:" + to_string(iid);
                ret += mqErrorMsg("evpuller", devSn, iid, "failed to set socksopt", zmq_setsockopt(pDealer, ZMQ_IDENTITY, ident.c_str(), ident.size()));
                ret += mqErrorMsg("evpuller", devSn, iid, "failed to connect to router " + urlDealer, zmq_connect(pDealer, urlDealer.c_str()));
                if(ret < 0) {
                    this_thread::sleep_for(chrono::seconds(3));
                    spdlog::error("evpuller {} {} zmq setup failed. retrying load config...", devSn, iid);
                    continue;
                }
            }
            catch(exception &e) {
                this_thread::sleep_for(chrono::seconds(3));
                spdlog::error("evpuller {} {} exception in EvPuller.init {:s}, retrying... ",devSn, iid, e.what());
                continue;
            }

            inited = true;
            spdlog::info("successfully load config");
        }

        return 0;
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
            if(pktCnt % 1024 == 0) {
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
    av_log_set_level(AV_LOG_INFO);
    spdlog::set_level(spdlog::level::info);
    //DB::exec(NULL, NULL, NULL,NULL);
    auto evp = EvPuller();
    evp.join();
    return 0;
}