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

#include "zmqhelper.hpp"
#include "tinythread.hpp"
#include "common.hpp"
#include "database.h"

using namespace std;
using namespace zmqhelper;

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

    int handleMsg(vector<vector<uint8_t> > &body) {
        int ret = 0;
        zmq_msg_t msg;

        cout<<endl<<endl;
        for(auto &j:body) {
            cout<<body2str(j) << "; ";
        }
        cout <<endl;

        // ID_SENDER, ID_TARGET, MSG
        if(body.size() != 3) {
            spdlog::error("evmgr {} illegal message received, frame num: {}", devSn, body.size());
            return -1;
        }

        // if need forward
        if(memcmp((void*)(body[1].data()), devSn.data(), body[1].size()) != 0) {
            spdlog::info("evmgr {} route msg from {} to {}", devSn, body2str(body[0]), body2str(body[1]));
            vector<vector<uint8_t> >v;
            v.push_back(body[1]);
            v.push_back(body[0]);
            v.push_back(body[2]);
            ret = z_send_multiple(pRouter, v);
            if(ret < 0) {
                spdlog::error("evmgr {} failed to send multiple: {}", devSn, zmq_strerror(zmq_errno()));
            }
        }else{
            // TODO: report msg
            spdlog::info("evmgr {} subsystem report msg received: {}; {}; {}", devSn, zmqhelper::body2str(body[0]), zmqhelper::body2str(body[1]), zmqhelper::body2str(body[2]));
        }

        return ret;
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
            auto body = z_recv_multiple(pRouter);
            if(body.size() == 0) {
                spdlog::error("evmgr {} failed to receive multiple msg: {}", devSn, zmq_strerror(zmq_errno()));
                continue;   
            }
            // full proto msg received.
            handleMsg(body);
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