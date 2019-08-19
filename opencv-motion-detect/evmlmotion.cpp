#pragma GCC diagnostic ignored "-Wunused-private-field"
#pragma GCC diagnostic ignored "-Wunused-variable"

#include <stdlib.h>
#include <string>
#include <thread>
#include <iostream>
#include <chrono>
#include <future>
#include <vector>

#ifdef OS_LINUX
#include <filesystem>
namespace fs = std::filesystem;
#endif
#include <cstdlib>
#include "vendor/include/zmq.h"
#include "tinythread.hpp"
#include "common.hpp"
#include "avcvhelpers.hpp"
#include "database.h"

using namespace std;

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

class EvMLMotion: public TinyThread {
private:
    void *pSubCtx = NULL, *pReqCtx = NULL; // for packets relay
    void *pSub = NULL, *pReq = NULL;
    string urlOut, urlPub, urlRep, sn;
    int iid;
    bool enablePush = false;
    AVFormatContext *pAVFormatInput = NULL;
    AVCodecContext *pCodecCtx = NULL;
    AVDictionary *pOptsRemux = NULL;
    DetectParam detPara = {25,200,-1,10,3,30};
    // load from db
    int streamIdx = -1;

    int init()
    {
        int ret = 0;
        bool inited = false;
        // TODO: read db to get sn
        sn = "ILS-3";
        iid = 3;
        while(!inited) {
            // req config
            json jr = cloudutils::registry(sn.c_str(), "evmlmotion", iid);
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
                spdlog::info("evmlmotion {} {} will connect to {} for sub, {} for req", sn, iid, urlPub, urlRep);

                data = jr["data"]["services"]["evml"];
                for(auto &j: data) {
                    try {
                        j.at("path").get_to(urlOut);
                    }
                    catch(exception &e) {
                        spdlog::warn("evslicer {} {} exception get params for storing slices: {}, using default: {}", sn, iid, e.what(), URLOUT_DEFAULT);
                        urlOut = URLOUT_DEFAULT;
                    }
                    ret = system(("mkdir -p " +urlOut).c_str());
                    if(ret == -1) {
                        spdlog::error("failed mkdir {}", urlOut);
                        return -1;
                    }
                    //TODO
                    break;
                }
            }
            catch(exception &e) {
                bcnt = true;
                spdlog::error("evmlmotion {} {} exception in EvPuller.init {:s},  retrying...", sn, iid, e.what());
            }

            if(bcnt || urlOut.empty()) {
                // TODO: waiting for command
                spdlog::warn("evmlmotion {} {} waiting for command & retrying", sn, iid);
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
            spdlog::error("evmlmotion failed connect to pub: {}, {}", sn, iid);
            return -1;
        }
        ret = zmq_connect(pSub, urlPub.c_str());
        if(ret != 0) {
            spdlog::error("evmlmotion {} {} failed create sub", sn, iid);
            return -2;
        }

        // setup req
        pReqCtx = zmq_ctx_new();
        pReq = zmq_socket(pReqCtx, ZMQ_REQ);
        spdlog::info("evmlmotion {} {} try create req to {}", sn, iid, urlRep);
        ret = zmq_connect(pReq, urlRep.c_str());

        if(ret != 0) {
            spdlog::error("evmlmotion {} {} failed create req to {}", sn, iid, urlRep);
            return -3;
        }

        spdlog::info("evmlmotion {} {} success setupMq", sn, iid);

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

        // req avformatcontext packet
        // send first packet to init connection
        zmq_msg_t msg;
        zmq_send(pReq, "hello", 5, 0);
        spdlog::info("evmlmotion {} {} success send hello", sn, iid);
        ret =zmq_msg_init(&msg);
        if(ret != 0) {
            spdlog::error("failed to init zmq msg");
            exit(1);
        }
        // receive packet
        ret = zmq_recvmsg(pReq, &msg, 0);
        spdlog::info("evmlmotion {} {} recv", sn, iid);
        if(ret < 0) {
            spdlog::error("evmlmotion {} {} failed to recv zmq msg: {}", sn, iid, zmq_strerror(ret));
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
    EvMLMotion()
    {
        init();
        setupMq();
        setupStream();
    };
    ~EvMLMotion() {};
};

int main(int argc, const char *argv[])
{
    spdlog::set_level(spdlog::level::info);
    EvMLMotion es;

#ifdef DEBUG
    // TODO: remove
    cv::namedWindow( "Display window", cv::WINDOW_AUTOSIZE );

    es.detach();
    // TODO: remove me
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