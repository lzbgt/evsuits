#pragma GCC diagnostic ignored "-Wunused-private-field"
#pragma GCC diagnostic ignored "-Wunused-variable"

#include <stdlib.h>
#include <string>
#include <thread>
#include <iostream>
#include <chrono>
#include <future>
#include <queue>

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
    json peerStatus;
    json jmgr;
    unordered_map<string, queue<vector<vector<uint8_t> >> > cachedMsg;
    mutex cacheLock;
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
                if(!config.count("data")||!config["data"].count(devSn)||!config["data"][devSn].count("ipcs")){
                    spdlog::error("evmgr {} invalid config. reload now...", devSn);
                    this_thread::sleep_for(chrono::seconds(3));
                    continue;
                }
                jmgr =  config["data"][devSn];
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
        // ID_SENDER, ID_TARGET, meta ,MSG
        if(body.size() != 4) {
            spdlog::warn("evmgr {} dropped a message, since its size is incorrect: {}", devSn, body.size());
            return 0;
        }

        if(memcmp((void*)(body[1].data()), (devSn +":0:0").data(), body[1].size()) != 0) {
            // message to other peer
            // check peer status
            string gid = body2str(body[1]);
            if(peerStatus.count(gid)!= 0) {
                auto t = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count() - peerStatus[gid].get<long long>();
                if(t > EV_HEARTBEAT_SECONDS*5/4){
                    peerStatus[gid] = 0;
                    // need cache
                }else{
                    spdlog::info("evmgr {} route msg from {} to {}", devSn, body2str(body[0]), body2str(body[1]));
                    vector<vector<uint8_t> >v = {body[1], body[0], body[2], body[3]};
                    ret = z_send_multiple(pRouter, v);
                    if(ret < 0) {
                        spdlog::error("evmgr {} failed to send multiple: {}", devSn, zmq_strerror(zmq_errno()));
                    }
                }
            }else{
                peerStatus[gid] = 0;
                // need cache
            }

            if(peerStatus[gid] == 0) {
                // cache
                spdlog::warn("evmgr {} cached msg from {} to {}", devSn, body2str(body[0]), gid);
                vector<vector<uint8_t> >v = {body[1], body[0], body[2], body[3]};
                lock_guard<mutex> lock(cacheLock);
                cachedMsg[gid].push(v);
                if(cachedMsg[gid].size() > EV_NUM_CACHE_PERPEER) {
                    cachedMsg[gid].pop();
                }
            }
        }else{
            // message to mgr
            spdlog::info("evmgr {} subsystem report msg received: {}; {}; {}", devSn, zmqhelper::body2str(body[0]), zmqhelper::body2str(body[1]), zmqhelper::body2str(body[2]));
            string meta = body2str(body[2]);
            string gid = body2str(body[0]);
            if(meta == "pong"||meta == "ping") {
                // update status
                this->peerStatus[gid] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
                if(meta=="ping") {
                    if(cachedMsg.find(gid) != cachedMsg.end()) {
                        while(!cachedMsg[gid].empty()){
                            lock_guard<mutex> lock(cacheLock);
                            auto v = cachedMsg[gid].front();
                            cachedMsg[gid].pop();
                            ret = z_send_multiple(pRouter, v);
                            if(ret < 0) {
                                spdlog::error("evmgr {} failed to send multiple: {}", devSn, zmq_strerror(zmq_errno()));
                            }  
                        }
                    }
                }
            }else{
                // TODO:
                spdlog::warn("evmgr {} received unknown meta {} from {}", devSn, meta, gid);
            }
        }

        return ret;
    }

protected:
    void run(){
        bool bStopSig = false;
        int ret = 0;
        zmq_msg_t msg;
        
        // health checking thread
        auto thHealth = thread([&,this](){
            auto ipcs = this->jmgr["ipcs"];
            json jmeta; jmeta["type"] = "ping";
            auto meta = str2body(jmeta.dump());
            auto mgrId = str2body(this->devSn + ":0:0");
            while(true) {
                for(auto &j:ipcs) {
                    if(j.count("modules") != 0) {
                        for(auto &[k, v]: j["modules"].items()) {
                            // k = module name
                            for(auto &m: v) {
                                if(!m.count("sn") && !m.count("iid")) {
                                    // construct gid for module
                                    string gid = m["sn"].get<string>() + ":" + k + ":" + to_string(m["iid"]);
                                    // build ping msg
                                    vector<vector<uint8_t> > v = {str2body(gid), mgrId, meta, str2body("hello")};
                                    ret = z_send_multiple(this->pRouter, v);
                                    if(ret < 0) {
                                        spdlog::error("evmgr {} failed to send ping to module {}", devSn, gid);
                                    }else{
                                        //
                                    }
                                }
                            }
                        }
                    }   
                }
                // TODO:
                this_thread::sleep_for(chrono::seconds(EV_HEARTBEAT_SECONDS));
            }
        });

        while (true) {
            if(checkStop() == true) {
                bStopSig = true;
                break;
            }
            auto body = z_recv_multiple(pRouter,false);
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
    spdlog::set_level(spdlog::level::debug);
    mgr.join();
    return 0;
}