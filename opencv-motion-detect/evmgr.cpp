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

/**
 *  functions:
 *  app update
 *  control msg
 *
 **/

class EvMgr:public TinyThread {
private:
    void *pRouterCtx = NULL;
    void *pRouter = NULL;
    json config;
    string devSn;

    void init()
    {
        int ret;
        bool inited = false;
        // TODO: load config from local db
        devSn = "ILSEVMGR1";
        while(!inited) {
            try {
                config = json::parse(cloudutils::config);
                spdlog::info("config dumps: \n{}", config.dump());
                // TODO: verify sn
                json jmgr =  config["data"][devSn];
                string proto = jmgr["proto"];
                string addr;
                
                if(proto != "zmq"){
                    spdlog::error("evmgr {} unsupported protocol: {}, try fallback to zmq instead now...", devSn, proto);
                }
                addr = "tcp://" + jmgr["addr"].get<string>() + ":" + to_string(jmgr["port-router"]);
                // setup zmq
                // TODO: connect to cloud

                // router service
                pRouterCtx = zmq_ctx_new();
                pRouter = zmq_socket(pRouterCtx, ZMQ_ROUTER);
                ret = zmq_bind(pRouter, addr.c_str());
                if(ret < 0) {
                    spdlog::error("evmgr {} failed to bind zmq at {} for reason: {}, retrying load configuration...", devSn, addr, zmq_strerror(zmq_errno()));
                    this_thread::sleep_for(chrono::seconds(3));
                    continue;
                }
                inited = true;
            }
            catch(exception &e) {
                spdlog::error("evmgr {} exception on init() for: {}, retrying load configuration...", devSn, e.what());
                this_thread::sleep_for(chrono::seconds(3));
                continue;
            }
        }
        spdlog::info("evmgr {} successfuly inited", devSn);
    }

    int mqErrorMsg(string cls, string devSn, string extraInfo, int ret) {
        if(ret < 0) {
            spdlog::error("{} {} {}:{} ", cls, devSn, extraInfo, zmq_strerror(zmq_errno()));
        }
        return ret;
    }

    void handleMsg(string body[]) {
        zmq_msg_t msg;
        if(body[0] != devSn) {
            for(int i =0; i < 3; i++) {
                spdlog::info("evmgr {}, msg idRcv is {}, forwarding...", devSn, body[0]);
                zmq_msg_init(&msg);
                zmq_msg_init_data(&msg, (void*)body[0].c_str(), body[0].size(), NULL, NULL);
                mqErrorMsg("evmgr", devSn, "failed to send zmq msg", zmq_send_const(pRouter, zmq_msg_data(&msg), body[0].size(), i ==2?0:ZMQ_SNDMORE));
                zmq_msg_close(&msg);
            }
        }else{
            // TODO: report msg
            spdlog::info("evmgr {} subsystem report msg received: {} {} {}", devSn, body[0], body[1], body[2]);
        }
    }

protected:
    void run(){
        bool bStopSig = false;
        int ret = 0;
        zmq_msg_t msg;
        while (true) {
            if(checkStop() == true) {
                bStopSig = true;
                break;
            }
            string msgBody[3];
            int64_t more = 0;
            // business logic
            int i = 0;
            for(; i < 3; i++) {
                mqErrorMsg("evmgr", devSn, "failed to init zmq msg", zmq_msg_init(&msg));
                mqErrorMsg("evmgr", devSn, "failed to recv zmq msg", zmq_recvmsg(pRouter, &msg, 0));
                msgBody[i] = string((char *)zmq_msg_data(&msg));
                zmq_msg_close(&msg);
                spdlog::debug("evmgr {} received[{}]: {} ", devSn, i, msgBody[i]);
                size_t more_size = sizeof (more);
                mqErrorMsg("evmgr", devSn, "failed to get zmq sockopt", zmq_getsockopt(pRouter, ZMQ_RCVMORE, &more, &more_size));
                if(!more) {
                    break;
                }
            }
            if(i >= 3 ) {
                // full proto msg received.
                handleMsg(msgBody);
            }else{
                spdlog::warn("partial msg recved, maybe hello msg: {}, {}, {}", msgBody[0], msgBody[1], msgBody[2]);
            }

        }
    }
public:
    EvMgr()
    {
        init();
    }
    ~EvMgr()
    {
        if(pRouter != NULL) {
            zmq_close(pRouter);
            pRouter = NULL;
        }
        if(pRouterCtx != NULL){
            zmq_ctx_destroy(pRouterCtx);
            pRouterCtx = NULL;
        }
    }
};

int main(int argc, const char *argv[])
{
    EvMgr mgr;
    mgr.join();
    return 0;
}