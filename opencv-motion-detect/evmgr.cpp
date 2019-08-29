/*
module: evmgr
description:
author: Bruce.Lu <lzbgt@icloud.com>
update: 2019/08/23
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
#include <queue>
#include <ctime>

#ifdef OS_LINUX
#include <filesystem>
namespace fs = std::filesystem;
#endif

#include "inc/zmqhelper.hpp"
#include "inc/tinythread.hpp"
#include "inc/common.hpp"
#include "inc/database.h"

using namespace std;
using namespace zmqhelper;

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
    queue<string> eventQue;
    mutex eventQLock;
    time_t tsLastBoot,  tsUpdateTime;

    //
    void init()
    {
        int ret;
        bool inited = false;
        // TODO: load config from local db
        json info;
        ret = LVDB::getSn(info);
        if(ret < 0) {
            spdlog::error("failed to get sn");
            exit(1);
        }

        tsLastBoot = info["lastboot"];
        tsUpdateTime=info["updatetime"];

        spdlog::info("evmgr info: sn = {}, lastboot = {}, updatetime = {}", info["sn"].get<string>(), ctime(&tsLastBoot), ctime(&tsUpdateTime));
        devSn = info["sn"];

        ret = LVDB::getLocalConfig(config);
        if(ret < 0) {
            spdlog::error("failed to get local configuration");
            exit(1);
        }
        // set all module status to 0
        ret = LVDB::traverseConfigureModules(config, [](string modname, json &m)->int{
            if(m.count("status") != 0)
            {
                //cout << modname <<" ," << m.dump() << endl;
                m["status"] = 0;
            }
            return 0;
        });
        if(ret < 0) {
            spdlog::error("evmgr {} failed to set module status to 0", devSn);
        }else{
            //spdlog::info("new config: {}", config.dump());
            LVDB::setLocalConfig(config);
        }

        int opt_notify = ZMQ_NOTIFY_DISCONNECT|ZMQ_NOTIFY_CONNECT;
        string proto, addr;
        while(!inited) {
            try {
                spdlog::info("config dumps: \n{}", config.dump());
                // TODO: verify sn
                if(!config.count("data")||!config["data"].count(devSn)||!config["data"][devSn].count("ipcs")) {
                    spdlog::error("evmgr {} invalid config. reload now...", devSn);
                    goto togo_sleep_continue;
                }
                jmgr =  config["data"][devSn];
                proto = jmgr["proto"];

                if(proto != "zmq") {
                    spdlog::warn("evmgr {} unsupported protocol: {}, try fallback to zmq instead now...", devSn, proto);
                }

                //
                if(jmgr["addr"].get<string>()  == "*" || jmgr["addr"].get<string>() == "0.0.0.0") {
                    spdlog::error("invalid mgr address: {} in config:\n{}", jmgr["addr"].get<string>(), jmgr.dump());
                    goto togo_sleep_continue;
                }

                //addr = "tcp://" + jmgr["addr"].get<string>() + ":" + to_string(jmgr["port-router"]);
                addr = "tcp://*:" + to_string(jmgr["port-router"]);
                // setup zmq
                // TODO: connect to cloud

                // router service
                pRouterCtx = zmq_ctx_new();
                pRouter = zmq_socket(pRouterCtx, ZMQ_ROUTER);
                zmq_setsockopt (pRouter, ZMQ_ROUTER_NOTIFY, &opt_notify, sizeof (opt_notify));
                ret = zmq_bind(pRouter, addr.c_str());
                if(ret < 0) {
                    spdlog::error("evmgr {} failed to bind zmq at {} for reason: {}, retrying load configuration...", devSn, addr, zmq_strerror(zmq_errno()));
                    goto togo_sleep_continue;
                }
                spdlog::info("evmgr {} bind success to {}", devSn, addr);
                inited = true;
                break;

togo_sleep_continue:
                this_thread::sleep_for(chrono::seconds(3));
                //continue;
            }
            catch(exception &e) {
                spdlog::error("evmgr {} exception on init() for: {}, retrying load configuration...", devSn, e.what());
                this_thread::sleep_for(chrono::seconds(3));
                continue;
            }
        }
        spdlog::info("evmgr {} successfuly inited", devSn);
    }

    int mqErrorMsg(string cls, string devSn, string extraInfo, int ret)
    {
        if(ret < 0) {
            spdlog::error("{} {} {}:{} ", cls, devSn, extraInfo, zmq_strerror(zmq_errno()));
        }
        return ret;
    }

    int handleMsg(vector<vector<uint8_t> > &body)
    {
        int ret = 0;
        zmq_msg_t msg;
        // ID_SENDER, ID_TARGET, meta ,MSG
        string selfId, peerId, meta;
        if(body.size() == 2 && body[1].size() == 0) {
            selfId = body2str(body[0]);
            bool eventConn = false;
            // XTF2BJR9:evslicer:1
            auto sp = cloudutils::split(selfId, ':');
            if(sp.size() != 3) {
                spdlog::warn("evmg {} inproper peer id: {}", devSn, selfId);
                return -1;
            }
            json *mod = LVDB::findConfigModule(config, sp[0], sp[1], stoi(sp[2]));
            if(mod == NULL) {
                spdlog::warn("evmgr {} failed to find module with id: {}", devSn, selfId);
                return -1;
            }

            if(peerStatus.count(selfId) == 0||peerStatus[selfId] == 0) {
                peerStatus[selfId] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
                spdlog::info("evmgr {} peer connected: {}", devSn, selfId);
                eventConn = true;
                mod->at("status") = 1;
                spdlog::debug("evmgr {} update status of {} to 1", devSn, selfId);
            }
            else {
                peerStatus[selfId] = 0;
                mod->at("status") = 0;
                spdlog::warn("evmgr {} peer disconnected: {}", devSn, selfId);
            }

            //update config
            ret = LVDB::setLocalConfig(config);
            if(ret < 0) {
                spdlog::error("evmgr {} failed to update localconfig", devSn);
            }

            // event
            json jEvt;
            jEvt["type"] = EV_MSG_TYPE_CONN_STAT;
            jEvt["gid"] = selfId;
            jEvt["ts"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
            if(eventConn) {
                jEvt["event"] = EV_MSG_EVENT_CONN_CONN;
            }
            else {
                jEvt["event"] = EV_MSG_EVENT_CONN_DISCONN;
            }

            eventQue.push(jEvt.dump());
            if(eventQue.size() > MAX_EVENT_QUEUE_SIZE) {
                eventQue.pop();
            }

            return 0;
        }
        else if(body.size() != 4) {
            spdlog::warn("evmgr {} dropped an invalid message, size: {}", devSn, body.size());
            return 0;
        }

        meta = body2str(body[2]);
        selfId = body2str(body[0]);
        peerId = body2str(body[1]);
        // update status;
        this->peerStatus[selfId] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();

        // msg to peer
        if(memcmp((void*)(body[1].data()), (devSn +":0:0").data(), body[1].size()) != 0) {
            // message to other peer
            // check peer status
            vector<vector<uint8_t> >v = {body[1], body[0], body[2], body[3]};
            if(peerStatus.count(peerId)!= 0 && peerStatus[peerId] != 0) {
                spdlog::info("evmgr {} route msg from {} to {}", devSn, selfId, peerId);
                ret = z_send_multiple(pRouter, v);
                if(ret < 0) {
                    spdlog::error("evmgr {} failed to send multiple: {}", devSn, zmq_strerror(zmq_errno()));
                }
            }
            else {
                // cache
                spdlog::warn("evmgr {} cached msg from {} to {}", devSn, selfId, peerId);
                lock_guard<mutex> lock(cacheLock);
                cachedMsg[peerId].push(v);
                if(cachedMsg[peerId].size() > EV_NUM_CACHE_PERPEER) {
                    cachedMsg[peerId].pop();
                }
            }

            // check if event
            try {
                string metaType = json::parse(meta)["type"];
                if(metaType == EV_MSG_META_EVENT) {
                    eventQue.push(body2str(body[3]));
                    if(eventQue.size() > MAX_EVENT_QUEUE_SIZE) {
                        eventQue.pop();
                    }
                }
            }
            catch(exception &e) {
                spdlog::error("evmgr {} exception parse event msg from {} to {}: ", devSn, selfId, peerId, e.what());
            }
        }
        else {
            // message to mgr
            // spdlog::info("evmgr {} subsystem report msg received: {}; {}; {}", devSn, zmqhelper::body2str(body[0]), zmqhelper::body2str(body[1]), zmqhelper::body2str(body[2]));
            if(meta == "pong"||meta == "ping") {
                // update status
                spdlog::info("evmgr {}, ping msg from {}", devSn, selfId);
                if(meta=="ping") {
                    if(cachedMsg.find(selfId) != cachedMsg.end()) {
                        while(!cachedMsg[selfId].empty()) {
                            lock_guard<mutex> lock(cacheLock);
                            auto v = cachedMsg[selfId].front();
                            cachedMsg[selfId].pop();
                            ret = z_send_multiple(pRouter, v);
                            if(ret < 0) {
                                spdlog::error("evmgr {} failed to send multiple: {}", devSn, zmq_strerror(zmq_errno()));
                            }
                        }
                    }
                }
            }
            else {
                // TODO:
                spdlog::warn("evmgr {} received unknown meta {} from {}", devSn, meta, selfId);
            }
        }

        return ret;
    }

protected:
    void run()
    {
        bool bStopSig = false;
        int ret = 0;
        zmq_msg_t msg;

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
    EvMgr(EvMgr &&) = delete;
    EvMgr(EvMgr &) = delete;
    EvMgr(const EvMgr &) = delete;
    EvMgr& operator=(const EvMgr &) = delete;
    EvMgr& operator=(EvMgr &&) = delete;
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
        if(pRouterCtx != NULL) {
            zmq_ctx_destroy(pRouterCtx);
            pRouterCtx = NULL;
        }
    }
};

int main(int argc, const char *argv[])
{
    av_log_set_level(AV_LOG_ERROR);
    spdlog::set_level(spdlog::level::info);
    EvMgr mgr;
    mgr.join();
    return 0;
}