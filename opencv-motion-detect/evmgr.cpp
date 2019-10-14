/*
module: evmgr
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
#include <queue>
#include <ctime>

#include "inc/zmqhelper.hpp"
#include "inc/tinythread.hpp"
#include "inc/common.hpp"
#include "inc/database.h"

using namespace std;
using namespace zmqhelper;

class EvMgr:public TinyThread {
private:
    void *pRouterCtx = nullptr;
    void *pRouter = nullptr;
    void *pCtxDealer = nullptr, *pDealer = nullptr;
    json config;
    string devSn, ident;
    json peerData;
    unordered_map<string, queue<vector<vector<uint8_t> >> > cachedMsg;
    mutex cacheLock;
    queue<string> eventQue;
    mutex eventQLock;
    time_t tsLastBoot,  tsUpdateTime;
    string drport = "5549";
    thread thCloudMsgHandler;

    int handleCloudMsg(vector<vector<uint8_t> > v)
    {
        int ret = 0;
        string peerId, metaType, metaValue, msg;
        json data;
        for(auto &b:v) {
            msg +=body2str(b) + ";";
        }

        bool bProcessed = false;
        if(v.size() == 3) {
            try {
                peerId = body2str(v[0]);
                json meta = json::parse(body2str(v[1]));
                metaType = meta["type"];
                if(meta.count("value") != 0) {
                    metaValue = meta["value"];
                }

                // msg from cluster mgr
                string daemonId = this->devSn + ":evdaemon:0";
                if(peerId == daemonId) {
                    if(metaValue == EV_MSG_META_VALUE_CMD_STOP || metaValue == EV_MSG_META_VALUE_CMD_RESTART) {
                        spdlog::info("evmgr {} received {} cmd from cluster mgr {}", devSn, metaValue, daemonId);
                        bProcessed = true;
                        exit(0);
                    }
                }
            }
            catch(exception &e) {
                spdlog::error("evmgr {} exception to process msg {}: {}", devSn, msg, e.what());
            }
        }

        if(!bProcessed) {
            spdlog::error("evmgr {} received msg having no implementation from peer: {}", devSn, msg);
        }

        return ret;
    }

    //
    void init()
    {
        int ret = 0;
        json jret;
        bool inited = false;
        int opt_notify = ZMQ_NOTIFY_DISCONNECT|ZMQ_NOTIFY_CONNECT;
        string addr;

        try {
            //
            spdlog::info("evmgr boot configuration: {} -> {}", devSn, config.dump());

            if(config["proto"] != "zmq") {
                spdlog::warn("evmgr {} unsupported protocol: {}, try fallback to zmq instead now...", devSn, config["proto"].get<string>());
            }

            //
            if(config["addr"].get<string>()  == "*" || config["addr"].get<string>() == "0.0.0.0") {
                spdlog::error("evmgr invalid mgr address: {} in config:\n{}", config["addr"].get<string>(), config.dump());
                goto error_exit;
            }

            addr = "tcp://*:" + to_string(config["portRouter"]);
            // setup zmq
            // TODO: connect to cloud

            // router service
            pRouterCtx = zmq_ctx_new();
            pRouter = zmq_socket(pRouterCtx, ZMQ_ROUTER);
            //ZMQ_TCP_KEEPALIVE
            //ZMQ_TCP_KEEPALIVE_IDLE
            //ZMQ_TCP_KEEPALIVE_INTVL
            ret = 1;
            zmq_setsockopt(pRouter, ZMQ_TCP_KEEPALIVE, &ret, sizeof (ret));
            ret = 20;
            zmq_setsockopt(pRouter, ZMQ_TCP_KEEPALIVE_IDLE, &ret, sizeof (ret));
            zmq_setsockopt(pRouter, ZMQ_TCP_KEEPALIVE_INTVL, &ret, sizeof (ret));
            zmq_setsockopt (pRouter, ZMQ_ROUTER_NOTIFY, &opt_notify, sizeof (opt_notify));
            ret = zmq_bind(pRouter, addr.c_str());
            if(ret < 0) {
                spdlog::error("evmgr {} failed to bind zmq at {} for reason: {}, retrying load configuration...", devSn, addr, zmq_strerror(zmq_errno()));
                goto error_exit;
            }
            spdlog::info("evmgr {} bind success to {}", devSn, addr);
            inited = true;
error_exit:
            if(inited) {
            }
            else {
                exit(1);
            }
        }
        catch(exception &e) {
            spdlog::error("evmgr {} exception on init() for: {}. abort booting up.", devSn, e.what());
            exit(1);
        }

        thCloudMsgHandler = thread([this] {
            while(true)
            {
                auto body = z_recv_multiple(pDealer,false);
                if(body.size() == 0) {
                    spdlog::error("evslicer {} failed to receive multiple msg: {}", this->devSn, zmq_strerror(zmq_errno()));
                    continue;
                }
                // full proto msg received.
                this->handleCloudMsg(body);
            }
        });
        thCloudMsgHandler.detach();   

        spdlog::info("evmgr {} successfuly inited", devSn);
    }

    int mqErrorMsg(string cls, string devSn, string extraInfo, int ret)
    {
        if(ret < 0) {
            spdlog::error("{} {} {}:{} ", cls, devSn, extraInfo, zmq_strerror(zmq_errno()));
        }
        return ret;
    }

    int handleEdgeMsg(vector<vector<uint8_t> > &body)
    {
        int ret = 0;
        zmq_msg_t msg;
        // ID_SENDER, ID_TARGET, meta ,MSG
        string selfId, peerId, meta;
        if(body.size() == 2 && body[1].size() == 0) {
            selfId = body2str(body[0]);
            bool eventConn = false;
            // XTF2BJR9:evslicer:1
            auto sp = strutils::split(selfId, ':');
            if(sp.size() != 3) {
                spdlog::warn("evmg {} inproper peer id: {}", devSn, selfId);
                return -1;
            }
            json *mod = LVDB::findConfigModule(config, sp[0], sp[1], stoi(sp[2]));
            if(mod == nullptr||peerData["status"].count(selfId) == 0) {
                spdlog::warn("evmgr {} failed to find the connecting/disconnecting module with id {} in config. please check if it was terminated correctly", devSn, selfId);
                return -1;
            }

            if(peerData["status"].count(selfId) == 0||peerData["status"][selfId] == 0) {
                peerData["status"][selfId] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
                spdlog::info("evmgr {} peer connected: {}", devSn, selfId);
                eventConn = true;
            }
            else {
                peerData["status"][selfId] = 0;
                spdlog::warn("evmgr {} peer disconnected: {}", devSn, selfId);
            }

            if(ret < 0) {
                spdlog::error("evmgr {} failed to update localconfig", devSn);
            }

            // event
            // json jEvt;
            // jEvt["type"] = EV_MSG_TYPE_CONN_STAT;
            // jEvt["gid"] = selfId;
            // jEvt["ts"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
            // if(eventConn) {
            //     jEvt["event"] = EV_MSG_EVENT_CONN_CONN;
            // }
            // else {
            //     jEvt["event"] = EV_MSG_EVENT_CONN_DISCONN;
            // }

            // eventQue.push(jEvt.dump());
            // if(eventQue.size() > MAX_EVENT_QUEUE_SIZE) {
            //     eventQue.pop();
            // }

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
        this->peerData["status"][selfId] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();

        // msg to peer
        string myId = devSn + ":evmgr:0";
        int minLen = std::min(body[1].size(), myId.size());
        if(memcmp((void*)(body[1].data()), myId.data(), minLen) != 0) {
            // message to other peer
            // check peer status
            vector<vector<uint8_t> >v = {body[1], body[0], body[2], body[3]};
            if(peerData["status"].count(peerId)!= 0 && peerData["status"][peerId] != 0) {
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

        while (true) {
            if(checkStop() == true) {
                bStopSig = true;
                break;
            }
            // if(1 == getppid()) {
            //     spdlog::error("evmgr {} exit since evdaemon is dead", devSn);
            //     exit(1);
            // }
            
            auto body = z_recv_multiple(pRouter,false);
            if(body.size() == 0) {
                spdlog::error("evmgr {} failed to receive multiple msg: {}", devSn, zmq_strerror(zmq_errno()));
                continue;
            }
            // full proto msg received.
            handleEdgeMsg(body);
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
        peerData["status"] = json();
        const char *strEnv = getenv("DR_PORT");
        if(strEnv != nullptr) {
            drport = strEnv;
        }

        strEnv = getenv("PEERID");
        if(strEnv != nullptr) {
            ident = strEnv;
            auto v = strutils::split(ident, ':');
            if(v.size() != 3||v[1] != "evmgr" || v[2] != "0") {
                spdlog::error("evmgr received invalid gid: {}", ident);
                exit(1);
            }

            devSn = v[0];
        }
        else {
            spdlog::error("evmgr failed to start. no SN set");
            exit(1);
        }

        //
        string addr = string("tcp://127.0.0.1:") + drport;;
        ident = devSn + ":evmgr:0";
        int ret = zmqhelper::setupDealer(&pCtxDealer, &pDealer, addr, ident);
        if(ret != 0) {
            spdlog::error("evmgr {} failed to setup dealer {}", devSn, addr);
            exit(1);
        }

        ret = zmqhelper::recvConfigMsg(pDealer, config, addr, ident);
        if(ret != 0) {
            spdlog::error("evmgr {} failed to receive configration message {}", devSn, addr);
        }

        init();
    }
    ~EvMgr()
    {
        if(pRouter != nullptr) {
            zmq_close(pRouter);
            pRouter = nullptr;
        }
        if(pRouterCtx != nullptr) {
            zmq_ctx_destroy(pRouterCtx);
            pRouterCtx = nullptr;
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