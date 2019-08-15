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
#include "spdlog/spdlog.h"
#define MAX_ZMQ_MSG_SIZE 1204 * 1024 * 2

using namespace std;

class PacketPusher: public TinyThread {
private:
    void *pSubCtx = NULL, *pReqCtx = NULL; // for packets relay
    void *pSub = NULL, *pReq = NULL;
    string urlOut, urlPub, urlRep, sn;
    int iid;
    bool enablePush = false;
    int *streamList = NULL;
    AVFormatContext *pAVFormatRemux = NULL;
    AVFormatContext *pAVFormatInput = NULL;

    int init()
    {
        bool inited = false;
        // TODO: read db to get sn
        sn = "ILS-2";
        iid = 2;
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
                spdlog::info("evpusher {} {} will connect to {} for sub, {} for req", sn, iid, urlPub, urlRep);

                data = jr["data"]["services"]["evpusher"];
                for(auto &j: data) {
                    if(j["sn"] == sn && iid == j["iid"] && j["enabled"] != 0) {
                        urlOut = j["urlDest"];
                        break;
                    }
                }
            }
            catch(exception &e) {
                bcnt = true;
                spdlog::error("evpusher {} {} exception in EvPuller.init {:s} retrying", sn, iid, e.what());
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
        }
        if(pSub != NULL) {
            zmq_ctx_destroy(pSub);
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

        ret = avformat_alloc_output_context2(&pAVFormatRemux, NULL, "rtsp", urlOut.c_str());
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
            spdlog::error("evpusher {} {} failed allocating output stream", sn ,iid);
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

            // decode
            pktCnt++;
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

            //calc pts
            {
                spdlog::debug("seq: {:lld}, pts: {:lld}, dts: {:lld}, dur: {:lld}, idx: {:d}", pktCnt, packet.pts, packet.dts, packet.duration, packet.stream_index);
                /* copy packet */
                packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);
                packet.pos = -1;
            }

            ret = av_interleaved_write_frame(pAVFormatRemux, &packet);
            av_packet_unref(&packet);
            if (ret < 0) {
                spdlog::error("error muxing packet");
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
    PacketPusher()
    {
        init();
        if(setupMq() < 0) {
            // TODO: reconnect
            exit(1);
        }
        setupStream();
    }

    ~PacketPusher()
    {
        teardownMq();
    }
};

int main(int argc, char *argv[])
{
    av_log_set_level(AV_LOG_INFO);
    spdlog::set_level(spdlog::level::debug);
    PacketPusher pusher;
    pusher.join();
}