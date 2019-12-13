/*
module: evdaemon
description: to monitor and configure all other components. runs only one instance per host.
author: Bruce.Lu <lzbgt@icloud.com>
created: 2019/09/04
update: 2019/09/10
*/

#include <queue>
#include <cstdlib>
#include <algorithm>
#include <regex>
#include <iterator>
#include <set>
#include <unistd.h>
#include <sys/wait.h>
#include <fstream>

#include "inc/tinythread.hpp"
#include "inc/httplib.h"
#include "inc/zmqhelper.hpp"
#include "inc/database.h"
#include "inc/json.hpp"
#include "inc/utils.hpp"
#include "inc/fs.h"
#include "networks.hpp"
#include "reverse_tun.hpp"

using namespace std;
using namespace httplib;
using namespace nlohmann;
using namespace zmqhelper;


#define EV_FILE_LVDB_DAEMON "/opt/lvldb/daemon"
#define EV_REVERSE_TUN_PIDS "/var/run/evdaemon-reverse-tun.pids"

class EvDaemon {
private:
    Server svr;
    json config;
    json oldConfig;
    json deltaCfg;
    json info;
    int port = 8088;
    thread thMon;
    string devSn, daemonId;
    int portRouter = 5549;
    thread::id thIdMain;
    thread thRouter;
    thread thCloud;
    bool bBootstrap = true;
    bool bColdStart = true;

    // peerData["status"];
    // peerData["pids"];
    // peerData["config"];
    json peerData;
    unordered_map<string, queue<vector<vector<uint8_t> >> > cachedMsg;
    mutex cacheLock;
    queue<string> eventQue;
    mutex eventQLock;

    mutex mutSubsystem;

    thread thHeartBeat;
    mutex mutHeartBeat;
    // bool bGotHeartBeat = false;
    int heartBeatTimeout = 60 * 1000;


    /// module gid to process id
    json mapModsToPids;

    // for zmq
    void *pRouterCtx = nullptr, *pRouter = nullptr;
    void *pDealerCtx = nullptr, *pDealer = nullptr;
    string cloudAddr = "tcp://127.0.0.1:5548";

    /// tracking sub-systems: evmgr, evpuller, evpusher, evml*, evslicer etc.
    json mapSubSystems;
    json jsonIPs;

    int ping(void *s)
    {
        int ret = 0;
        json data;
        data["ips"] = this->jsonIPs;
        data["reports"] = json();
        json modIdsOnline;
        json modIdsOffline;
        for(auto &[k,v]: this->peerData["status"].items()) {
            // ignore evmgr
            if(k.find("evmgr") != string::npos) {
                continue;
            }
            if(v != 0 && v != -1 && v != 1 && v != 2) {
                modIdsOnline.push_back(k);
            }
            else {
                modIdsOffline.push_back(k);
            }
        }

        // // since offline with be default status, don't need to send explicitly
        // for(const string &k: modIdsOffline) {
        //     json report;
        //     string msg = fmt::format("evdaemon [ping] {} detects module {} offline", this->devSn, k);
        //     report["msg"] = msg;
        //     report["modId"] = k;
        //     report["type"] = EV_MSG_META_TYPE_REPORT;
        //     report["catId"] = EV_MSG_REPORT_CATID_AVMODOFFLINE;
        //     report["level"] = EV_MSG_META_VALUE_REPORT_LEVEL_ERROR;
        //     report["time"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
        //     report["status"] = "active";
        //     report["type"] = EV_MSG_META_TYPE_REPORT;
        //     report["value"] = EV_MSG_META_VALUE_REPORT_LEVEL_ERROR;
        //     data["reports"].push_back(report);
        // }

        for(const string &k: modIdsOnline) {
            json report;
            string msg = fmt::format("evdaemon [ping] {} detects module {} online", this->devSn, k);
            report["msg"] = msg;
            report["modId"] = k;
            report["type"] = EV_MSG_META_TYPE_REPORT;
            report["catId"] = EV_MSG_REPORT_CATID_AVMODOFFLINE;
            report["level"] = EV_MSG_META_VALUE_REPORT_LEVEL_ERROR;
            report["time"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
            report["status"] = "recover";
            report["type"] = EV_MSG_META_TYPE_REPORT;
            report["value"] = EV_MSG_META_VALUE_REPORT_LEVEL_ERROR;
            data["reports"].push_back(report);
        }

        vector<string> modIdsLoopRecover;
        for(auto &[k,v] :peerData["contConn"].items()) {
            if(v > 4) {
                // not offline
                if(peerData["status"].count(k) != 0 && peerData["status"][k] != -1 && peerData["status"][k] != 2 && peerData["status"][k] != 1) {
                    auto now = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
                    auto delta = now - peerData["status"][k].get<decltype(now)>();
                    if(delta > 22) {
                        modIdsLoopRecover.push_back(k);
                        v = 0;
                    }
                }
            }
        }

        {
            json report;
            report["type"] = EV_MSG_META_TYPE_REPORT;
            report["catId"] = EV_MSG_REPORT_CATID_AVLOOPRESTART;
            report["level"] = EV_MSG_META_VALUE_REPORT_LEVEL_ERROR;
            report["time"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
            report["status"] = "recover";
            report["type"] = EV_MSG_META_TYPE_REPORT;
            report["value"] = EV_MSG_META_VALUE_REPORT_LEVEL_ERROR;
            for(auto &k: modIdsLoopRecover) {
                string msg = fmt::format("evdaemon [ping] {} detects module {} recovered from loop restarting", this->devSn, k);
                report["modId"] = k;
                report["msg"] = msg;
                data["reports"].push_back(report);
            }
        }



        vector<vector<uint8_t> >body = {str2body("evcloudsvc:0:0"), str2body(EV_MSG_META_PING), str2body(data.dump())};

        ret = z_send_multiple(s, body);
        if(ret < 0) {
            spdlog::error("evdaemon {} failed to send ping: {}", devSn, zmq_strerror(zmq_errno()));
        }
        return ret;
    }

    int reloadCfg(string subModGid = "")
    {
        int ret = LVDB::getSn(this->info);
        if(ret < 0) {
            spdlog::error("evdaemon {} failed to get info", this->devSn);
            return 1;
        }

        this->devSn = this->info["sn"];
        this->daemonId = this->devSn + ":evdaemon:0";

        // apply config
        try {
            // lock_guard<mutex> lock(cacheLock);
            json &data = this->config;
            string peerId;
            for(auto &[k,v]:data.items()) {
                if(k == this->devSn) {
                    peerId = v["sn"].get<string>() + ":evmgr:0";
                    this->peerData["config"][peerId] = v;
                    if(this->peerData["status"].count(peerId) == 0) {
                        this->peerData["status"][peerId] = -1; // unkown
                    }
                    else {
                        // nop
                    }
                    this->peerData["enabled"][peerId] = 1;
                    spdlog::info("evdaemon {} config for submodule {} loaded", devSn, peerId);
                }

                // startup other submodules
                json &ipcs = v["ipcs"];
                for(auto &ipc : ipcs) {
                    json &modules = ipc["modules"];
                    for(auto &[mn, ml] : modules.items()) {
                        for(auto &m : ml) {
                            if(m["sn"] != this->devSn) {
                                continue;
                            }

                            string peerName;
                            ret = cfgutils::getPeerId(mn, m, peerId, peerName);
                            if(ret != 0) {
                                continue;
                            }

                            if(m.count("enabled") == 0 || m["enabled"] == 0) {
                                spdlog::warn("evdaemon {} {} was disabled", this->devSn, peerId);
                                this->peerData["enabled"][peerId] = 0;
                            }
                            else {
                                this->peerData["enabled"][peerId] = 1;
                            }

                            this->peerData["config"][peerId] = v;
                            if(this->peerData["status"].count(peerId) == 0) {
                                this->peerData["status"][peerId] = -1; // unkown
                            }
                            else {
                                // nop
                            }

                            spdlog::info("evdaemon {} config for submodule {} loaded", devSn, peerId);
                        }
                    }
                }
            }
        }
        catch(exception &e) {
            spdlog::error("evdaemon {} exception reload and apply configuration: {}:\n{}", this->devSn, e.what(), this->config.dump());
            return -1;
        }

        return 0;
    }

    void cleanupSubSystems()
    {
        spdlog::info("evdaemon {} peerData {}", this->devSn, this->peerData.dump());
        json &pd = this->peerData;
        for(auto &[k,v]: pd["pids"].items()) {
            //kill(v, SIGTERM);
            if(this->peerData["status"].count(k) != 0) {
                this->peerData["status"].erase(k);
            }

            // if(this->peerData["config"].count(k) != 0){
            //     this->peerData["config"].erase(k);
            // }
            if(this->peerData["pids"].count(k) != 0) {
                this->peerData["pids"].erase(k);
            }
        }
    }

    int startSubSystems(vector<string> subs = {})
    {
        int ret = 0;
        std::lock_guard<std::mutex> lock(mutSubsystem);
        if(subs.size() != 0) {
            for(auto &k: subs) {
                if((this->peerData["status"].count(k) == 0 || this->peerData["status"][k] == 0 ||  (this->peerData["status"][k] != 1 &&  this->peerData["status"][k] != 2)) && this->peerData["config"].count(k) != 0 &&  this->peerData["enabled"].count(k) != 0 && this->peerData["enabled"][k] != 0) {
                    pid_t pid;

                    if(this->peerData["contConn"].count(k) != 0 && this->peerData["contConn"][k] > 4) { // 4 times
                        json meta;
                        json data;
                        string msg = fmt::format("evdaemon {} detects module {} is restarting frequently, should tabke action", this->devSn, k);
                        spdlog::error(msg);
                        data["msg"] = msg;
                        data["modId"] = k;
                        data["type"] = EV_MSG_META_TYPE_REPORT;
                        data["catId"] = EV_MSG_REPORT_CATID_AVLOOPRESTART;
                        data["level"] = EV_MSG_META_VALUE_REPORT_LEVEL_ERROR;
                        data["time"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
                        data["status"] = "recover";
                        meta["type"] = EV_MSG_META_TYPE_REPORT;
                        meta["value"] = EV_MSG_META_VALUE_REPORT_LEVEL_ERROR;
                        z_send(pDealer, "evcloudsvc", meta.dump(), data.dump());
                        // this_thread::sleep_for(chrono::seconds(10));
                    }
                    this->peerData["status"][k] = 0;
                    ret = zmqhelper::forkSubsystem(devSn, k, portRouter, pid);
                    if(0 == ret) {
                        this->peerData["pids"][k] = pid;
                        spdlog::info("evdaemon {} created subsystem {} (current status: {})", this->devSn, k, this->peerData["status"][k].get<int>());
                    }
                    else {
                        spdlog::info("evdaemon {} failed to create subsystem {}", this->devSn, k);
                    }
                }
                else {
                    spdlog::warn("evdaemon {} refuse to start subsystem {}, maybe it's disabled or removed out from lastes cluster config", this->devSn, k);
                }
            }
        }
        else {
            if(this->bColdStart) {
                vector<string> tmp;
                json unkown;
                vector<string> terms;
                string info;
                int cnt = 0;
                for(auto &[k,v]: this->peerData["config"].items()) {
                    if(this->peerData["enabled"].count(k) != 0 && this->peerData["enabled"][k] != 0) {
                        if((this->peerData["status"].count(k) == 0 || this->peerData["status"][k] == 0)) {
                            tmp.push_back(k);
                            info += (cnt == 0? "" : string(", ")) + k;
                        }
                        else if(this->peerData["status"][k] == -1) {
                            unkown[k] = -1;
                        }
                    }
                    else {
                        terms.push_back(k);
                    }
                    cnt++;
                }

                spdlog::info("evdaemon {} will start following subsystems: {}", devSn, info);
                //
                for(string &e : tmp) {
                    pid_t pid = 0;
                    this->peerData["status"][e] = 0;
                    ret = zmqhelper::forkSubsystem(devSn, e, portRouter, pid);
                    if(0 == ret) {
                        this->peerData["pids"][e] = pid;
                        spdlog::info("evdaemon {} created subsystem {}", devSn, e);
                    }
                    else {
                        spdlog::info("evdaemon {} failed to create subsystem {}", devSn, e);
                    }
                }

                for(string &e: terms) {
                    if(this->peerData["pids"].count(e) != 0) {
                        kill(this->peerData["pids"][e], SIGTERM);
                    }
                }

                while(unkown.size() != 0 && cnt < 3) {
                    this_thread::sleep_for(chrono::seconds(3));
                    for(auto &[k,v]: unkown.items()) {
                        if(this->peerData["status"][k] != -1 && this->peerData["status"][k] != 0) {
                            // no need to start
                            unkown.erase(k);
                        }
                    }
                    cnt++;
                }

                for(auto &[k,v]: unkown.items()) {
                    pid_t pid = 0;
                    this->peerData["status"][k] = 0;
                    ret = zmqhelper::forkSubsystem(devSn, k, portRouter, pid);
                    if(0 == ret) {
                        this->peerData["pids"][k] = pid;
                        spdlog::info("evdaemon {} created subsystem {}", devSn, k);
                    }
                    else {
                        spdlog::info("evdaemon {} failed to create subsystem {}", devSn, k);
                    }
                }
                this->bColdStart = false;
            }
            else {
                // calc diff
                auto jret = cfgutils::getModulesOperFromConfDiff(this->oldConfig, this->config, this->deltaCfg, this->devSn);
                this->deltaCfg = json();
                if(jret["code"] != 0) {
                    spdlog::error("evdaemon {} invalid config received. will revert to old config and restart. {}: {}", this->devSn, this->config.dump(), jret["msg"].get<string>());
                    // revert config and restart
                    ret = LVDB::setLocalConfig(this->oldConfig, "", EV_FILE_LVDB_DAEMON);
                    kill(getpid(), SIGTERM);
                }

                json &mods = jret["data"];
                if(mods.size() == 0) {
                    spdlog::info("evdaemon {} startSubSystems: no module to operate", this->devSn);
                    return ret;
                }

                // there is diff. restart evmgr first
                string mgrId = this->devSn + ":evmgr:0";
                sendCmd2Peer(mgrId, EV_MSG_META_VALUE_CMD_STOP, "0");

                for(auto &[k,v]: mods.items()) {
                    spdlog::info("evdaemon {} startSubSystems config diff to module action: {} -> {}", this->devSn, string(k), int(v));
                    if(v == 0) {
                        // stop
                        this->peerData["status"][k] = 1; // disabled
                        sendCmd2Peer(k, EV_MSG_META_VALUE_CMD_STOP, "0");
                    }
                    else if(v == 1) {
                        this->peerData["status"][k] = 2; // perm stop
                        this->peerData["config"].erase(k);
                        sendCmd2Peer(k, EV_MSG_META_VALUE_CMD_STOP, "0");
                    }

                    else if(int(v) == 2||int(v) == 3) {
                        int status = (this->peerData["status"].count(k) == 0) ? -1:this->peerData["status"][k].get<int>();
                        spdlog::info("evdaemon {} module {} status {}", this->devSn, k, status);
                        // not running
                        if(this->peerData["status"].count(k) == 0 || this->peerData["status"][k] == 0||this->peerData["status"][k] == -1||this->peerData["status"][k]== 1||this->peerData["status"][k] == 2) {
                            pid_t pid;
                            spdlog::info("evdaemon {} starting subsystem {}", this->devSn, k);
                            this->peerData["status"][k] = 0;
                            ret = zmqhelper::forkSubsystem(devSn, k, portRouter, pid);
                            if(0 == ret) {
                                this->peerData["pids"][k] = pid;
                                spdlog::info("evdaemon {} created subsystem {}", this->devSn, k);
                            }
                            else {
                                spdlog::info("evdaemon {} failed to create subsystem {}", this->devSn, k);
                            }
                        }
                        else {
                            sendCmd2Peer(k, EV_MSG_META_VALUE_CMD_STOP, to_string(v));
                        }
                    }
                    else {
                        //
                        spdlog::warn("evdaemon {} unkown action {} for module {}", this->devSn, int(v), string(k));
                    }
                }
            }
        }
        return ret;
    }

    int sendCmd2Peer(string peerId, string cmdVal, string msg)
    {
        json meta;
        meta["type"] = EV_MSG_META_TYPE_CMD;
        meta["value"] = cmdVal;
        int ret = z_send(pRouter, peerId, this->daemonId, meta, msg);
        if(ret < 0) {
            spdlog::error("evdaemon {} failed to send msg to peer {}: {} - {}", devSn, peerId, meta.dump(), msg);
        }
        else {
            spdlog::info("evdaemon {} successfully send msg to peer {}: {} - {}", devSn, peerId, meta.dump(), msg);
        }
        return ret;
    }

    void sendModConnectedMsg(json &modIds)
    {
        json meta;
        json data;
        string msg = fmt::format("evdaemon {} detects modules {} booted up", this->devSn, modIds.dump());
        data["msg"] = msg;
        data["modId"] = modIds;
        data["type"] = EV_MSG_META_TYPE_REPORT;
        data["catId"] = EV_MSG_REPORT_CATID_AVMODOFFLINE;
        data["level"] = EV_MSG_META_VALUE_REPORT_LEVEL_ERROR;
        data["time"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
        data["status"] = "recover";
        meta["type"] = EV_MSG_META_TYPE_REPORT;
        meta["value"] = EV_MSG_META_VALUE_REPORT_LEVEL_ERROR;
        z_send(pDealer, "evcloudsvc", meta.dump(), data.dump());
    }


    void sendModOfflineMsg(json &modIds)
    {
        json meta;
        json data;
        string msg = fmt::format("evdaemon {} detects modules {} offline", this->devSn, modIds.dump());
        data["msg"] = msg;
        data["modId"] = modIds;
        data["type"] = EV_MSG_META_TYPE_REPORT;
        data["catId"] = EV_MSG_REPORT_CATID_AVMODOFFLINE;
        data["level"] = EV_MSG_META_VALUE_REPORT_LEVEL_ERROR;
        data["time"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
        data["status"] = "active";
        meta["type"] = EV_MSG_META_TYPE_REPORT;
        meta["value"] = EV_MSG_META_VALUE_REPORT_LEVEL_ERROR;
        z_send(pDealer, "evcloudsvc", meta.dump(), data.dump());
    }

    int handleEdgeMsg(vector<vector<uint8_t> > &body)
    {
        int ret = 0;
        // ID_SENDER, ID_TARGET, meta ,MSG
        string selfId, peerId, meta;
        // connection message
        if(body.size() == 2 && body[1].size() == 0) {
            selfId = body2str(body[0]);
            bool eventConn = false;
            // XTF2BJR9:evslicer:1
            auto sp = strutils::split(selfId, ':');
            if(sp.size() != 3) {
                spdlog::warn("evdaemon {} inproper peer id: {}", devSn, selfId);
                return -1;
            }

            //auto state = zmq_socket_get_peer_state(pRouter, selfId.data(), selfId.size());
            //spdlog::info("evdaemon {} peerState: {}", devSn, state);

            json modIds;
            modIds.push_back(selfId);
            if((peerData["status"].count(selfId) == 0 || peerData["status"][selfId] == 0||this->peerData["status"][selfId] == -1) ) {
                peerData["status"][selfId] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
                spdlog::info("evdaemon {} peer connected: {}", devSn, selfId);
                if(selfId.find("evmgr") == string::npos) {
                    sendModConnectedMsg(modIds);
                }

                if(this->peerData["tsLastConn"].count(selfId) == 0) {
                    this->peerData["tsLastConn"][selfId] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
                }
                else {
                    if(this->peerData["contConn"].count(selfId) == 0) {
                        this->peerData["contConn"][selfId] = 0;
                    }
                    else {
                        auto delta = this->peerData["contConn"][selfId].get<long>() - chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
                        if(delta < 3) { // within 3s
                            this->peerData["contConn"][selfId] =  this->peerData["contConn"][selfId].get<int>() + 1;
                        }
                        else {
                            this->peerData["contConn"][selfId] = 0;
                        }
                        // refer to startSubsystems
                    }
                    this->peerData["tsLastConn"][selfId] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
                }

                eventConn = true;
                string cfg = peerData["config"][selfId].dump();
                spdlog::debug("evdaemon {} peer {} config is: {}", devSn, selfId, cfg);
                json j;
                j["type"] = EV_MSG_META_CONFIG;
                string meta = j.dump();
                vector<vector<uint8_t> > v = {str2body(selfId), str2body(this->daemonId), str2body(meta), str2body(cfg)};
                z_send_multiple(pRouter, v);
                spdlog::info("evdaemon {} peer {} config sent: {}", devSn,selfId, cfg);
            }
            else {
                if(selfId.find("evmgr") == string::npos) {
                    sendModOfflineMsg(modIds);
                }

                if(peerData["status"][selfId] == 1 || peerData["status"][selfId] == 2) {
                    spdlog::warn("evdaemon {} refuse to start {}: it was asked to be stopped, and is removed from cluster config", this->devSn, selfId);
                }
                else {
                    peerData["status"][selfId] = 0;
                    if(peerData["pids"].count(selfId) != 0) {
                        peerData["pids"].erase(selfId);
                    }

                    if(this->bBootstrap) {
                        spdlog::warn("evdaemon {} peer {} disconnected. restarting it.", devSn, selfId);
                        startSubSystems({selfId});
                    }
                    else {
                        spdlog::warn("evdaemon {} peer {} disconnected. won't restart it since BOOTSTRAP=false", devSn, selfId);
                    }
                }
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
            spdlog::warn("evdaemon {} dropped an invalid message, size: {}", devSn, body.size());
            return 0;
        }

        meta = body2str(body[2]);
        selfId = body2str(body[0]);
        peerId = body2str(body[1]);
        // update status;
        this->peerData["status"][selfId] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();

        // msg to other peer
        int minLen = std::min(body[1].size(), this->daemonId.size());
        if(memcmp((void*)(body[1].data()), this->daemonId.data(), minLen) != 0) {
            // message to other peer

            // check if target is evcloudsvc
            if(peerId == "evcloudsvc") {
                zmqhelper::z_send(pDealer, peerId, body[2], body[3]);
            }
            else {
                vector<vector<uint8_t> >v = {body[1], body[0], body[2], body[3]};
                if(peerData["status"].count(peerId) != 0 && peerData["status"][peerId] != 0 && this->peerData["status"][peerId][peerId] != -1) {
                    spdlog::info("evdaemon {} route msg from {} to {}", devSn, selfId, peerId);
                    ret = z_send_multiple(pRouter, v);
                    if(ret < 0) {
                        spdlog::error("evdaemon {} failed to send multiple: {}", devSn, zmq_strerror(zmq_errno()));
                    }
                }
                else {
                    // cache
                    spdlog::warn("evdaemon {} cached msg from {} to {}", devSn, selfId, peerId);
                    lock_guard<mutex> lock(cacheLock);
                    cachedMsg[peerId].push(v);
                    if(cachedMsg[peerId].size() > EV_NUM_CACHE_PERPEER) {
                        cachedMsg[peerId].pop();
                    }
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
                spdlog::error("evdaemon {} exception parse event msg from {} to {}: ", devSn, selfId, peerId, e.what());
            }
        }
        else {
            // message to mgr
            // spdlog::info("evdaemon {} subsystem report msg received: {}; {}; {}", devSn, zmqhelper::body2str(body[0]), zmqhelper::body2str(body[1]), zmqhelper::body2str(body[2]));
            if(meta == "pong"||meta == "ping") {
                // update status
                spdlog::info("evdaemon {}, ping msg from {}", devSn, selfId);
                if(meta=="ping") {
                    //get pid
                    //this->peerData["pids"][selfId] = stoi(body2str(body[3]));
                    if(cachedMsg.find(selfId) != cachedMsg.end()) {
                        while(!cachedMsg[selfId].empty()) {
                            lock_guard<mutex> lock(cacheLock);
                            auto v = cachedMsg[selfId].front();
                            cachedMsg[selfId].pop();
                            ret = z_send_multiple(pRouter, v);
                            if(ret < 0) {
                                spdlog::error("evdaemon {} failed to send multiple: {}", devSn, zmq_strerror(zmq_errno()));
                            }
                            else {
                                spdlog::info("evdaemon {} failed to send cached msg from {} to {} of type: {}, content: {}",devSn, body2str(v[1]), body2str(v[0]), body2str(v[2]), body2str(v[3]));
                            }
                        }
                    }
                }
            }
            else {
                spdlog::warn("evdaemon {} received unknown meta {} from {}", devSn, meta, selfId);
            }
        }

        return ret;
    }

    int manageReverseTun(bool bStart, json &tunCfg)
    {
        int ret = 0;
        if(tunCfg.count("port") == 0 || tunCfg.count("host") == 0 || tunCfg.count("user") == 0 || tunCfg.count("password") == 0) {
            spdlog::error("evcdaemon {} invalid reverse tunnel settings, shall have host, port, user, password fields");
            return -1;
        }
        thread th;
        string host = tunCfg["host"], user = tunCfg["user"], password = tunCfg["password"];
        int port = tunCfg["port"];
        th = thread([host, port, user, password]() {
            createReverseTun(host, port, user, password);
        });

        spdlog::info("evdaemon {} created reverse tun to {}:{}", devSn, tunCfg["host"].get<string>(), tunCfg["port"].get<int>());
        th.detach();

        return ret;
    }

    int handleCloudMsg(vector<vector<uint8_t> > &v)
    {
        int ret = 0;
        // ID_SENDER, meta ,MSG
        string msg;
        string peerId, meta;
        for(auto &s:v) {
            msg += body2str(s) + ";";
        }

        spdlog::info("evdaemon {} received msg from cloud: {}", devSn, msg);

        if(v.size() != 3) {
            spdlog::error("evdaemon {} received invalid msg from evcloudsvc {}", devSn, msg);
        }

        else {
            try {
                string meta = json::parse(v[1])["type"];
                string peerId = body2str(v[0]);
                json data = json::parse(body2str(v[2]));

                // from cloudsvc
                if(peerId == "evcloudsvc") {
                    // its configuration message
                    if(meta == EV_MSG_META_CONFIG) {
                        if(data.size() == 0) {
                            spdlog::error("evdaemon {} received invalid empty config", devSn);
                        }
                        else {
                            this->deltaCfg = json::diff(this->config, data);
                            spdlog::info("evdaemon {} received cloud config diff: {}\nnew: {}", devSn, this->deltaCfg.dump(4), data.dump());
                            if(this->deltaCfg.size() != 0 || this->bColdStart) {
                                this->oldConfig = this->config;
                                this->config = data;
                                spdlog::info("evdaemon {} reloading config from cloud", devSn);
                                ret = reloadCfg();
                                if(ret != 0) {
                                    spdlog::error("evdameon {} failed to parse new config: {}", devSn, data.dump());
                                    return ret;
                                }
                                ret = LVDB::setLocalConfig(data, "", EV_FILE_LVDB_DAEMON);
                                if(ret != 0) {
                                    spdlog::error("evdameon {} failed to save new config to local db: {}", devSn, data.dump());
                                    return ret;
                                }
                            }
                            else {
                            }

                            if(bBootstrap) {
                                startSubSystems();
                            }
                            else {
                                spdlog::info("evdaemon {} skip startup subsystems since BOOTSTRAP is set to false", devSn);
                            }
                        }
                    }
                    else if(meta == EV_MSG_META_TYPE_CMD) {
                        spdlog::info("evdaemon {} received cmd from cloud: {}", devSn, msg);
                        if(data.count("target") != 0 && data["target"].is_string() && data.count("metaType") !=0  && data["metaType"].is_string() &&
                                data.count("data") != 0 && data["data"].is_object() && data.count("metaValue") !=0  && data["metaValue"].is_string()) {
                            string target = data["target"];
                            auto v = strutils::split(target, ':');
                            if(v.size() == 1) {
                                if(data["metaValue"] == EV_MSG_META_VALUE_CMD_REVESETUN) {
                                    manageReverseTun(true, data["data"]);
                                }else if(data["metaValue"] == EV_MSG_META_VALUE_CMD_UPDATE){
                                    //
                                }
                                else {
                                    spdlog::info("evdaemon {} received msg {} from cloud to itself. but has no implementation for", devSn, data.dump());
                                }
                            }
                            else if(v.size() == 3) {
                                if(this->peerData["status"].count(target) == 0 || this->peerData["status"][target] == 0 || this->peerData["status"] == -1) {
                                    spdlog::error("evdaemon {} received {} msg from cloud to {}: {}, but its offline", devSn, meta, target, data.dump());
                                }
                                else {
                                    ret = sendCmd2Peer(target, data["metaValue"], data.dump());
                                    if(ret < 0) {
                                        spdlog::error("evdaemon {} failed to send msg to peer {}: {}", devSn, data.dump(), zmq_strerror(zmq_errno()));
                                    }
                                    else {
                                        spdlog::info("evdaemon {} successfully relayed {} msg from cloud to {}: {}", devSn, meta, target, data.dump());
                                    }
                                }
                            }
                            else {
                                spdlog::info("well");
                            }

                        }
                        else {
                            spdlog::info("done");
                        }
                    }
                    else if(meta == EV_MSG_META_PONG) {
                        string info;
                        if(data.count("data") != 0 ) {
                            if(data["data"].is_string()) {
                                info = fmt::format("evdaemon {} received pong msg from evcloudsvc: {}", devSn, data["data"].get<string>());
                            }
                            else if (data["data"].is_object()) {
                                info = fmt::format("evdaemon {} received pong msg from evcloudsvc: {}", devSn, data["data"].dump());
                            }
                        }
                        else {
                            info = fmt::format("evdaemon {} received pong msg from evcloudsvc.", devSn);
                        }
                        spdlog::info(info);
                    }
                    else {
                        spdlog::info("evdaemon {} received msg from cloud that having no handler implemented: {}", devSn, msg);
                    }
                }
                else {
                    // from peer
                    spdlog::info("evdaemon {} msg from peer {}: {}", devSn, peerId, data.dump());
                }

            }
            catch(exception &e) {
                spdlog::error("evdaemon {} file {}:{} exception {}", devSn, __FILE__, __LINE__, e.what());
            }
        }
        return 0;
    }

    void setUpDealer()
    {
        lock_guard<mutex> lg(mutHeartBeat);
        if(pDealer != nullptr) {
            int i = 0;
            zmq_setsockopt(pDealer, ZMQ_LINGER, &i, sizeof(i));
            zmq_close(pDealer);
            pDealer = nullptr;
        }

        if(pDealerCtx != nullptr) {
            zmq_ctx_destroy(pDealerCtx);
            pDealerCtx = nullptr;
        }

        int ret = 0;
        ret = zmqhelper::setupDealer(&pDealerCtx, &pDealer, cloudAddr, devSn, heartBeatTimeout);
        if(ret != 0) {
            spdlog::error("evdaemon {} failed to setup dealer", devSn);
            exit(1);
        }
        spdlog::info("evdaemon {} connecting to cloud {}", devSn, cloudAddr);
    }

protected:
public:
    void run()
    {
        //setupSubsystems();
        // get config
        svr.Get("/info", [this](const Request& req, Response& res) {
            LVDB::getSn(this->info);
            res.set_content(this->info.dump(), "text/json");
        });

        svr.Post("/info", [this](const Request& req, Response& res) {
            json ret;
            ret["code"] = 0;
            ret["msg"] = "ok";
            string sn = req.get_param_value("sn");
            if(sn.empty()) {
                ret["code"] = 1;
                ret["msg"] = "no sn in param";
            }
            else {
                json info;
                info["sn"] = sn;
                info["lastboot"] =  chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
                LVDB::setSn(info);
                ret["data"] = info;
            }
            res.set_content(ret.dump(), "text/json");
        });

        svr.Get("/config", [this](const Request& req, Response& res) {
            LVDB::getLocalConfig(this->config);
            res.set_content(this->config.dump(), "text/json");
        });

        svr.Post("/config", [](const Request& req, Response& res) {
            json ret;
            ret["code"] = 0;
            ret["msg"] = "ok";
            ret["time"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
            try {
                json newConfig;
                newConfig["data"] = json::parse(req.body)["data"];

                LVDB::setLocalConfig(newConfig);
                spdlog::info("evmgr new config: {}", newConfig.dump());
            }
            catch(exception &e) {
                ret.clear();
                ret["code"] = -1;
                ret["msg"] = e.what();
                ret["data"] = req.body;
            }
            res.set_content(ret.dump(), "text/json");
        });

        svr.Get("/reset", [](const Request& req, Response& res) {
            json ret;
            ret["code"] = 0;
            ret["msg"] = "resetting ...";
            res.set_content(ret.dump(), "text/json");
            kill(getpid(), SIGTERM);
        });

        svr.listen("0.0.0.0", 8088);
    }

    EvDaemon()
    {

        // killall subsystems

        /// peerId -> value
        peerData["status"] = json();
        peerData["pids"] = json();
        peerData["config"] = json();
        deltaCfg = json();
        int ret = 0;
        string dir_ = string("mkdir -p ") + EV_FILE_LVDB_DAEMON;
        system(dir_.c_str());
        // get sn of device
        json info;
        try {
            ret = LVDB::getSn(info);
        }
        catch(exception &e) {
            spdlog::error("evdaemon failed to get local info: {}", e.what());
            ret = -1;
        }
        if(ret != 0) {
            spdlog::error("fatal error: evdaemon failed to get/set SN");
            exit(1);
        }

        spdlog::info("evdaemon boot \n{}",info.dump());

        auto ipAddrs = netutils::getIps();
        if(ipAddrs.size() == 0) {
            spdlog::error("evdaemon failed to start: no network available!");
            exit(2);
        }

        for(const auto &[k, v]: ipAddrs) {
            jsonIPs.push_back(k);
        }

        devSn = info["sn"];

        char* strEnv = getenv("BOOTSTRAP");
        if(strEnv != nullptr && memcmp(strEnv, "false", 5) == 0) {
            bBootstrap = false;
        }

        // http port
        strEnv = getenv("DAEMON_PORT");
        if(strEnv != nullptr) {
            port = stoi(strEnv);
        }

        json cfg;
        ret = LVDB::getLocalConfig(cfg, "", EV_FILE_LVDB_DAEMON);
        if(ret < 0) {
            spdlog::info("evdaemon {} no local config", devSn);
        }
        else {
            this->config = cfg;
            // load localconfig
            spdlog::info("evdaemon {} local config: {}", devSn, cfg.dump());
            this->reloadCfg();
            if(bBootstrap) {
                this->startSubSystems();
            }
        }

        // zmq router port
        strEnv = getenv("ROUTER_PORT");
        if(strEnv != nullptr) {
            portRouter = stoi(strEnv);
        }

        string addr = string("tcp://*:") + to_string(portRouter);

        // setup router
        ret = zmqhelper::setupRouter(&pRouterCtx, &pRouter, addr);
        if(ret < 0) {
            spdlog::error("evdaemon {} setup router: {}", this->devSn, addr);
            exit(1);
        }
        // setup edge msg processor
        thRouter = thread([this]() {
            while(true) {
                auto v = zmqhelper::z_recv_multiple(this->pRouter);
                if(v.size() == 0) {
                    spdlog::error("evdaemon {} failed to receive msg {}", this->devSn, zmq_strerror(zmq_errno()));
                }
                else {
                    handleEdgeMsg(v);
                }
            }
        });
        thRouter.detach();

        spdlog::info("evdaemon {} edge message processor had setup {}", devSn, addr);

        // dealer port
        strEnv = getenv("CLOUD_ADDR");
        if(strEnv != nullptr) {
            cloudAddr = strEnv;
        }

        setUpDealer();

        // setup cloud msg processor
        thCloud = thread([this]() {
            int cnt = 0;
            while(true) {
                spdlog::info("evdaemon {} waiting msg from evcloudsvc", this->devSn);
                auto v = zmqhelper::z_recv_multiple(this->pDealer, false);
                if(v.size() == 0) {
                    if(cnt > 1) {
                        // TODO: reset dealer socket;
                        spdlog::error("evdaemon {} failed to receive from evcloudsvc, resetting connection: {}", this->devSn, zmq_strerror(zmq_errno()));
                        this->setUpDealer();
                        cnt = 0;
                        continue;
                    }
                    cnt++;
                }
                else {
                    cnt = 0;
                    this->handleCloudMsg(v);
                    spdlog::info("evdaemon {} successfully handled msg from evcloudsvc", this->devSn);
                }
            }
        });
        thCloud.detach();
        spdlog::info("evdaemon {} cloud message processor had setup {}", devSn, cloudAddr);

        thHeartBeat = thread([this]() {
            while(true) {
                {
                    lock_guard<mutex> lg(this->mutHeartBeat);
                    if(this->pDealer != nullptr)
                        this->ping(this->pDealer);
                }
                this_thread::sleep_for(chrono::milliseconds(this->heartBeatTimeout));
            }
        });
        thHeartBeat.detach();

        this->thIdMain = this_thread::get_id();
    };
    ~EvDaemon() {};
};

void cleanup(int signal)
{
    while (waitpid((pid_t) (-1), 0, WNOHANG) > 0) {}
}

int main()
{
    signal(SIGCHLD, cleanup);
    //sigignore(SIGCHLD);
    EvDaemon srv;
    srv.run();
}