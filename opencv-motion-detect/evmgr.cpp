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

class PacketProducer: public TinyThread {
private:
    void *pPubContext = NULL; // for packets publishing
    void *pPublisher = NULL;
    AVFormatContext *pAVFormatInput = NULL;
    string urlIn;
    int *streamList = NULL, numStreams = 0;
    
public:
  PacketProducer(string urlIn):urlIn(urlIn){
    setupMq();
  }

  ~PacketProducer(){
  }

protected:
    // Function to be executed by thread function
    void run()
    {
        int ret = 0;
        setupMq();
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
        int pktCnt = 0;
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
            pktCnt++;
            packet.stream_index = streamList[packet.stream_index];

            /* copy packet */
            //packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            //packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            //packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);
            //packet.pos = -1;

            // serialize packet to raw bytes
            char * data = NULL;
            int size = AVPacketSerializer::encode(packet, &data);
            zmq_msg_init_data(&msg, (void*)data, size, mqPacketFree, NULL);
            zmq_send_const(pPublisher, zmq_msg_data(&msg), size, 0);
            
            av_packet_unref(&packet);
        }

        // TODO:
        if(ret < 0 && !bStopSig) {
            // reconnect
        }else {
            std::cout << "Task End" << std::endl;
        }
    }

    private:
    int setupMq()
    {
        teardownMq();
        pPubContext = zmq_ctx_new();
        pPublisher = zmq_socket(pPubContext, ZMQ_PUB);

        int rc = zmq_bind(pPublisher, "tcp://0.0.0.0:5556");
        if(rc != 0) {
            spdlog::error("failed create pub");
        }

        return 0;
    }

    int teardownMq()
    {
        if(pPublisher != NULL) {
            zmq_close(pPublisher);
        }
        if(pPubContext != NULL) {
            zmq_ctx_destroy(pPubContext);
        }
        return 0;
    }
};



class EdgeVideoMgr {
private:
#define SECS_SLICE (60*5/2)
    AVFormatContext *pAVFormatInput = NULL, *pAVFormatRemux = NULL;
    AVCodec *pCodec = NULL;
    AVDictionary *pOptsRemux = NULL, *pOptsInput = NULL, *pOptsOutput = NULL;
    int idxVideo = -1, idxAudio = -1, numStreams = 0, numSlices = 6, secsSlice = SECS_SLICE;
    int *streamList = NULL;
    bool bPush = true, bRecord = false;
    string urlIn, urlOut, pathSlice;
    unordered_map<string, string> envParams = unordered_map<string, string>();
    // mq
    void *pRepContext = NULL; // for msg from edge gateway
    void *pReqContext = NULL; // for msg to edge gateway
private:
    void setupParams()
    {
        char *tmp = getenv("URL_IN");
        urlIn = (tmp == NULL?string(""): string(tmp));

        tmp= getenv("URL_OUT");
        urlOut = (tmp == NULL?string(""): string(tmp));

        tmp = getenv("SLICE_NUM");
        numSlices = (tmp == NULL?6:atoi(tmp));
        if(numSlices <=2) {
            numSlices = 6;
        }

        spdlog::info("in: {}", urlIn);

        tmp = getenv("SLICE_PATH");
        pathSlice = (tmp == NULL?string("slices"):string(tmp));

        // OSX XCode doesn't ship with the filesystem header as of version 10.x
#ifdef __LINUX___
        if (!fs::exists(pathSlice.c_str())) {
            if (!fs::create_directory(pathSlice.c_str())) {
                spdlog::error("can't create directory: {}", pathSlice.c_str());
                exit(1);
            }
            fs::permissions(pathSlice.c_str(), fs::perms::all);
        }
#endif

        tmp = getenv("PUSH");
        bPush = (tmp == NULL?false: (string(tmp) == string("false")?false:true));

        tmp = getenv("SLICE_SECS");
        secsSlice = (tmp == NULL?SECS_SLICE:atoi(tmp));
        if(secsSlice < SECS_SLICE) {
            secsSlice = SECS_SLICE;
        }

        if(urlIn == "" or urlOut == "") {
            spdlog::error("no input/output url");
            exit(1);
        }
    }

    int setupStreams()
    {
        int ret = 0;
        PacketProducer packetProducer(urlIn);
        packetProducer.join();
        // std::this_thread::sleep_for(std::chrono::milliseconds(30000));
        // packetProducer.stop();
        return ret;
    }

public:
    // ctor
    EdgeVideoMgr()
    {
        setupParams();
        setupStreams();
    }
    // dtor
    ~EdgeVideoMgr()
    {
        avformat_close_input(&pAVFormatInput);
        /* close output */
        if (pAVFormatRemux && !(pAVFormatRemux->oformat->flags & AVFMT_NOFILE))
            avio_closep(&pAVFormatRemux->pb);
        avformat_free_context(pAVFormatRemux);
        av_freep(&streamList);
    }
};


int main(int argc, char **argv)
{
    spdlog::set_level(spdlog::level::debug);
    DB::exec(NULL, NULL, NULL ,NULL);
    spdlog::info("hello");
    auto vp = EdgeVideoMgr();
    return 0;
}

