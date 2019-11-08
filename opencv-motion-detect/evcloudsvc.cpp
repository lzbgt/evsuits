/*
module: evcloudsvc
description: mqtt & http svc on cloud for evsuits
author: Bruce.Lu <lzbgt@icloud.com>
created: 2019/09/05
update: 2019/09/10
*/

#include <chrono>
#include <set>
#include <queue>
#include <mutex>
#include <algorithm>
#include "inc/tinythread.hpp"
#include "inc/httplib.h"
#include "inc/database.h"
#include "inc/json.hpp"
#include "inc/spdlog/spdlog.h"
#include "utils.hpp"
#include "inc/zmqhelper.hpp"
#include "fmt/format.h"

using namespace std;
using namespace httplib;
using namespace nlohmann;
using namespace zmqhelper;

//
#define KEY_CONFIG_MAP "configmap"
#define KEY_RELEASE_BUNDLE "release_bundle"

#define NUM_MAX_REPORT_HISTORY 5

class EvCloudSvc {
private:
    Server svr;
    void *pRouterCtx = nullptr, *pRouter = nullptr;
    string httpPort = "8089";
    string msgPort = "5548";
    string devSn = "evcloudsvc";

    json configMap;
    json releaseBundle;

    // peer data
    json peerData;
    unordered_map<string, queue<vector<vector<uint8_t> >> > cachedMsg;
    mutex cacheLock;
    queue<string> eventQue;
    mutex eventQLock;
    thread thMsgProcessor;

    int buildIpcStatus(json &conf) {
        int ret = 0;
        string msg;
        for(auto &[k,v]: conf.items()){
            try {
                json &ipcs = v["ipcs"];
                int ipcIdx = 0;
                json shadowObj;
                for(auto &ipc : ipcs) {
                    json &modules = ipc["modules"];
                    string ipcSn = ipc["sn"];
                    for(auto &[mn, ma]: modules.items()) {
                        for(auto &m:ma) {
                            string modGid;
                            string sn = m["sn"];
                            string mcls;
                            // ml
                            if(mn == "evml") {
                                modGid = sn +":evml" + m["type"].get<string>();
                                mcls = string("evml") + m["type"].get<string>();
                            }
                            else {
                                modGid = sn + ":" + mn;
                                mcls = mn;
                            }
                            this->peerData["modulecls"][mcls] = 1;

                            modGid = modGid + ":" + to_string(m["iid"].get<int>());

                            //{// start populate shadow object
                            if(shadowObj.count(ipcSn) == 0) {
                                shadowObj[ipcSn] = json();
                            }
                            auto &shad = shadowObj[ipcSn];

                            if(shad.count("mgrTerminal") == 0) {
                                shad["mgrTerminal"] = json();
                                shad["mgrTerminal"]["sn"] = k;
                                shad["mgrTerminal"]["online"] = false;
                            }
                            if(shad.count("expected") == 0) {
                                shad["expected"] = json();
                            }
                            if(shad.count("current") == 0) {
                                shad["current"] = json();
                            }
                            if(shad.count("issues") == 0) {
                                shad["issues"] = json();
                            }

                            if(shad.count("lastNReports") == 0) {
                                shad["lastNReports"] = json();
                            }

                            bool enabled = m["enabled"] == 0?false:true;

                            // mod2ipc
                            peerData["mod2ipc"][modGid] = ipcSn;

                            // mgrsn2ipc
                            if(peerData["mgr2ipc"].count(k) == 0) {
                                peerData["mgr2ipc"][k] = json();
                            }
                            peerData["mgr2ipc"][k][ipcSn] = 1;

                            if(shad["expected"].count(modGid) != 0) {
                                //multiple mod with same class
                                spdlog::error("{} configuration for ipc {} in dev {} having multiple modules {}. ignored that extra module", devSn, ipcSn, k, modGid);
                            }else{
                                shad["expected"][modGid] = enabled;
                                shad["current"][modGid] = false;
                            }
                            //}//

                        }// for mod
                    } // for modules
                    
                    ipcIdx++;
                }// for ipc

                // merge
                auto &ipcStatus = peerData["ipcStatus"];
                for(auto &[k,v]: shadowObj.items()){
                    if(ipcStatus.count(k) == 0) {
                        ipcStatus[k] = v;
                    }else{
                        auto &newCurr = v["current"];
                        auto &oldCurr = ipcStatus[k]["current"];
                        auto &newExpected = v["expected"];
                        auto &oldExpected = ipcStatus[k]["expected"];


                        vector<string> modRemove;
                        for(auto &[m,n]: oldCurr.items()){
                            if(newCurr.count(m) == 0) {
                                modRemove.push_back(m);
                            }
                        }

                        for(auto &k:modRemove) {
                            oldCurr.erase(k);
                            oldExpected.erase(k);
                            peerData["mod2ipc"].erase(k);

                            // remove issue
                            if(ipcStatus[k]["issues"].count(k) != 0) {
                                ipcStatus[k]["issues"].erase(k);
                            }
                        }

                        for(auto &[m,n]: newCurr.items()){
                            oldExpected[m] = newExpected[m];
                            if(oldCurr.count(m) == 0|| (oldCurr[m] == true && newExpected[m] == false)) {
                                oldCurr[m] = newCurr[m];
                            }
                        }
                    }
                }
            }
            catch(exception &e) {
                msg = fmt::format("evcloudsvc buildIpcStatus exception: {}", e.what());
                spdlog::error(msg);
            }
        }

        return ret;
    }

    void loadConfigMap()
    {
        // load configmap
        json cnfm;
        int ret = LVDB::getValue(cnfm, KEY_CONFIG_MAP);
        if(ret < 0 || cnfm.size() == 0) {
            this->configMap["sn2mods"] = json();
            this->configMap["mod2mgr"] = json();

            int iret = LVDB::setValue(this->configMap, KEY_CONFIG_MAP);
            if(iret >= 0) {
            }
            else {
                spdlog::error("evcloudsvc failed to save configmap");
                exit(1);
            }
        }
        else {
            this->configMap = cnfm;
        }

        // populate peerData
        for(auto &[k,v]: this->configMap["sn2mods"].items()) {
            // load config from database
            json cfg;
            if(LVDB::getLocalConfig(cfg, k) < 0) {
                spdlog::error("evcloudsvc failed to load config for device: {}", k);
            }
            else {
                this->peerData["config"][k] = cfg;
                spdlog::info("evcloudsvc loaded config for device: {}", k);
            }
        }

        // build ipcStatus
        buildIpcStatus(this->peerData["config"]);

        // release bundle
        json relBund;
        ret = LVDB::getValue(relBund, KEY_RELEASE_BUNDLE);
        if(ret <0 || relBund.size() == 0) {
            this->releaseBundle["bundles"] = json();
            this->releaseBundle["activeIdx"] = -1;
        }
        else {
            this->releaseBundle = relBund;
        }
    }

    int sendConfig(json &config_, string sn)
    {
        int ret = 0;
        string cfg = config_.dump();
        json j;
        j["type"] = EV_MSG_META_CONFIG;
        string meta = j.dump();
        vector<vector<uint8_t> > v = {str2body(sn), str2body(devSn), str2body(meta), str2body(cfg)};

        // if(peerData["online"].count(sn) == 0||peerData["online"][sn] == 0) {
        //     spdlog::warn("evcloudsvc {} cached config to {}", devSn, sn);
        //     lock_guard<mutex> lock(cacheLock);
        //     cachedMsg[sn].push(v);
        //     if(cachedMsg[sn].size() > EV_NUM_CACHE_PERPEER) {
        //         cachedMsg[sn].pop();
        //     }
        // }else{
        ret = z_send_multiple(pRouter, v);
        if(ret <0) {
            spdlog::error("evcloudsvc failed to send config to {}", sn);
        }
        else {
            spdlog::info("evcloudsvc config sent to {}: {}", sn, cfg);
        }

        //}

        return ret;
    }

    /// v: edge cluster mgr config
    json applyClusterCfg(string k, json& v, json& shadowObj)
    {
        json ret;
        ret["code"] = 0;
        ret["msg"] = "ok";
        string msg = "evcloudsvc may gracefully or strictly handled below issues for you. please correct them:\n";
        bool hasError = false;

        try {
            // construct sn2mods, mod2mgr
            if(v.count("ipcs") == 0||v["ipcs"].size() == 0 || v.count("sn") == 0 || v["sn"] != k) {
                msg += fmt::format("\t\tedge cluster {} has no sn/ipcs: {}.", k, v.dump());
                ret["msg"] = msg;
            }
            else {
                json &ipcs = v["ipcs"];
                int ipcIdx = 0;
                for(auto &ipc : ipcs) {
                    if(hasError) {
                        break;
                    }
                    if(ipc.count("modules") == 0||ipc["modules"].size() == 0||ipc.count("sn") == 0||ipc["sn"].size() == 0) {
                        msg += fmt::format("\tedge cluster {} has no modules for ipc {}", k, ipcIdx);
                        ret["msg"] = msg;
                    }
                    else {
                        json &modules = ipc["modules"];
                        string ipcSn = ipc["sn"];
                        for(auto &[mn, ma]: modules.items()) {
                            if(hasError) {
                                break;
                            }
                            if(ma.size() == 0) {
                                msg += fmt::format("/{}/ipcs/{}/modules/{} empty", k, ipcIdx, mn);
                                ret["msg"] = msg;
                                continue;
                            }
                            int modIdx = 0;
                            for(auto &m:ma) {
                                if(hasError) {
                                    break;
                                }
                                if(m.size() == 0) {
                                    msg+= fmt::format("\t\t/{}/ipcs/{}/modules/{}/{} empty", k, ipcIdx, mn, modIdx);
                                    ret["msg"] = msg;
                                    continue;
                                }
                                if(m.count("sn") == 0 || m["sn"].size() == 0 || m.count("iid") == 0 || m["iid"].size() == 0||m.count("enabled") == 0 || m["enabled"].size() == 0||(mn == "evml" && (m.count("type") == 0||m["type"].size() == 0))) {
                                    msg = fmt::format("evcloudsvc received invalid config at /{}/ipcs/{}/modules/{}/{}. check for fields sn, iid, type(evml): {}", k, ipcIdx, mn, modIdx, v.dump());
                                    spdlog::error(msg);
                                    hasError = true;
                                    break;
                                }

                                string modKey;
                                string sn = m["sn"];
                                if(sn.find('/', 0) != string::npos) {
                                    string msg = fmt::format("evcloudsvc invalid sn({}) in module /{}/ipcs/{}/modules/{} in config: {}", sn, k, ipcIdx, mn, modIdx, v.dump());
                                    spdlog::error(msg);
                                    hasError = true;
                                    break;
                                }

                                // ml
                                string mcls;
                                if(mn == "evml") {
                                    modKey = sn +":evml" + m["type"].get<string>();
                                    mcls = string("evml") + m["type"].get<string>();
                                }
                                else {
                                    modKey = sn + ":" + mn;
                                    mcls = mn;
                                }

                                this->peerData["modulecls"][mcls] = 1;

                                // modules
                                if(this->configMap["sn2mods"].count(sn) == 0) {
                                    this->configMap["sn2mods"][sn] = json();
                                }

                                // check exist of modkey
                                if(this->configMap["sn2mods"][sn].count(modKey) == 0) {
                                    this->configMap["sn2mods"][sn][modKey] = 1;
                                }

                                //{// start populate shadow object
                                if(shadowObj.count(ipcSn) == 0) {
                                    shadowObj[ipcSn] = json();
                                }
                                auto &shad = shadowObj[ipcSn];
    
                                if(shad.count("mgrTerminal") == 0) {
                                    shad["mgrTerminal"] = json();
                                    shad["mgrTerminal"]["sn"] = k;
                                    shad["mgrTerminal"]["online"] = false;
                                }
                                if(shad.count("expected") == 0) {
                                    shad["expected"] = json();
                                }
                                if(shad.count("current") == 0) {
                                    shad["current"] = json();
                                }
                                if(shad.count("issues") == 0) {
                                    shad["issues"] = json();
                                }

                                bool enabled = m["enabled"] == 0? false: true;
                                string modGid = sn + ":" + mn + ":" + to_string(m["iid"].get<int>());
                                // string modNick =  mn + ":" + to_string(m["iid"].get<int>());
                                if(shad["expected"].count(modGid) != 0) {
                                    //multiple mod with same class
                                    spdlog::error("{} configuration for ipc {} in dev {} having multiple modules {}. ignored that extra module", devSn, ipcSn, k, modGid);
                                }else{
                                    shad["expected"][modGid] = enabled;
                                    shad["current"][modGid] = false;
                                }
                                //}

                                // modkey -> sn_of_evmgr
                                this->configMap["mod2mgr"][modKey] = k;

                            }// for mod
                        } // for modules
                    }
                    ipcIdx++;
                }// for ipc
            }
        }
        catch(exception &e) {
            msg = fmt::format("evcloudsvc applyClusterCfg exception: {}", e.what());
            ret["msg"] = msg;
            spdlog::error(msg);
        }

        if(hasError) {
            ret["code"] = -1;
        }
        if(ret["msg"] != "ok") {
            spdlog::error(ret["msg"].get<string>());
        }

        return ret;
    }

    json config(json &newConfig)
    {
        json ret;
        int iret;
        json oldConfigMap = this->configMap;
        json oldPeerData = this->peerData;
        ret["code"] = 0;
        ret["msg"] = "ok";
        ret["time"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
        string msg;
        bool hasError = false;
        try {
            json deltaCfg = json();
            if(newConfig.count("data") == 0|| newConfig["data"].size() == 0) {
                string msg = fmt::format("evcloudsvc invalid configuratin body received - empty or no data field: {}", newConfig.dump());
                ret["code"] = 1;
                ret["msg"] = msg;
                spdlog::error(msg);
                hasError = true;
            }
            else {
                json &data = newConfig["data"];
                // for edge clusters, those are mgrs
                for(auto &[k, v]: data.items()) {
                    if(k.find('/', 0) != string::npos) {
                        ret["code"] = 2;
                        string msg = fmt::format("evcloudsvc invalid sn({}) as key in config: {}", k, data.dump());
                        ret["msg"] = msg;
                        spdlog::error(msg);
                        hasError = true;
                        break;
                    }
                    if(this->configMap.count(k) ^ this->peerData["config"].count(k)) {
                        spdlog::warn("evcloudsvc inconsistent configuration for cluster {}", k);
                        // TODO: improvements?
                        // remove both
                        this->configMap.erase(k);
                        this->peerData["config"].erase(k);
                    }

                    if(v.size() == 0) {
                        // ignore
                        continue;
                    }
                    if(this->configMap.count(k) == 0) {
                        // both not exist, fresh new
                        this->configMap[k] = k;
                        this->peerData["config"][k] = json();
                    }

                    // both exist, calc diff
                    json srcJson, targetJson;
                    srcJson[k] = this->peerData["config"][k];
                    targetJson[k] = v;
                    json diff = json::diff(srcJson, targetJson);
                    spdlog::info("evcloudsvc patching: \n\told: {},\n\tnew: {}", srcJson.dump(), targetJson.dump());
                    if(diff.size() == 0) {
                        spdlog::info("evcloudsvc no diffrence for cluster {}", k);
                        deltaCfg[k] = 1;
                    }
                    else {
                        auto gids = cfgutils::getModulesOperFromConfDiff(srcJson, targetJson, diff, "");
                        spdlog::info("dump gids: {}", gids.dump());
                        if(gids["code"] != 0) {
                            hasError = true;
                            msg = gids["msg"];
                            break;
                        }
                        if(gids["msg"] != "ok") {
                            ret["msg"] = gids["msg"];
                        }

                        for(auto &[a,b]: gids["data"].items()) {
                            string devSn = strutils::split(a, ':')[0];
                            deltaCfg[devSn] = 1;
                        }
                    }

                    json shadowStat;
                    auto r = applyClusterCfg(k,v, shadowStat);
                    if(r["code"] != 0) {
                        hasError = true;
                        msg = r["msg"];
                        break;
                    }

                    if(r["msg"] != "ok") {
                        ret["msg"] = r["msg"];
                    }


                    // update configmap for cluster config
                    this->configMap[k] = k;
                    this->peerData["config"][k] = v;
                    iret = LVDB::setLocalConfig(v, k);
                    if(iret < 0) {
                        hasError = true;
                        msg = fmt::format("evcloudsvc failed to save config for cluster {}: {} ", k, v.dump());
                        spdlog::error(msg);
                        ret["code"] = iret;
                        ret["msg"] = msg;
                    }

                } // for clusters

                buildIpcStatus(newConfig["data"]);

                if(!hasError) {
                    // save configmap
                    iret = LVDB::setValue(this->configMap, KEY_CONFIG_MAP);
                    if(iret < 0) {
                        msg = "evcloudsvc failed to save configmap";
                        spdlog::error(msg);
                        ret["code"] = iret;
                        hasError = true;
                        ret["msg"] = msg;
                    }
                    else {
                        // update config
                        for(auto &[x,y]: deltaCfg.items()) {
                            json j = getConfigForDevice(x);
                            if(j["code"] == 0) {
                                sendConfig(j["data"], x);
                            }
                        }

                        ret["data"] = newConfig["data"];
                    }
                }

                if(hasError) {
                    this->configMap = oldConfigMap;
                    this->peerData = oldPeerData;
                }
            }
        }
        catch(exception &e) {
            ret.clear();
            ret["code"] = -1;
            ret["msg"] = string("evcloudsvc exception: ") + e.what();
            spdlog::error("evcloudsvc exception: {}", e.what());
        }

        return ret;
    }

    //
    bool handleConnection(string selfId)
    {
        bool ret = false;
        int state = zmq_socket_get_peer_state(pRouter, selfId.data(), selfId.size());
        spdlog::info("evcloudsvc peer {} state: {}", selfId, state);
        if(peerData["online"].count(selfId) == 0 || peerData["online"][selfId] == 0) {
            peerData["online"][selfId] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
            spdlog::info("evcloudsvc peer connected: {}", selfId);
            ret = true;
            spdlog::debug("evcloudsvc update online status of {} to 1 and send config", selfId);
            json data = getConfigForDevice(selfId);
            if(data["code"] != 0) {
                json resp;
                resp["target"] = selfId,
                resp["metaType"] = EV_MSG_META_PONG;
                resp["data"] = data["msg"];
                sendEdgeMsg(resp);
            }
            else {
                sendConfig(data["data"], selfId);
            }

            if(peerData["mgr2ipc"].count(selfId) != 0) {
                for(auto &[k,v]: peerData["mgr2ipc"][selfId].items()){
                    if(peerData["ipcStatus"].count(k) == 0){
                        spdlog::error("{} no ipcStatus config for camera {}", devSn, k);
                    }else{
                        auto &ipcStatus = peerData["ipcStatus"][k];
                        if(ipcStatus["issues"].count(selfId) != 0) {
                            ipcStatus["issues"].erase(selfId);
                        }
                    }
                }
            }
        }
        else {
            peerData["online"][selfId] = 0;
            spdlog::warn("{} peer disconnected: {}", devSn, selfId); 
            if(peerData["mgr2ipc"].count(selfId) != 0) {
                for(auto &[k,v]: peerData["mgr2ipc"][selfId].items()){
                    if(peerData["ipcStatus"].count(k) == 0){
                        spdlog::error("{} no ipcStatus config for camera {}", devSn, k);
                    }else{
                        auto &ipcStatus = peerData["ipcStatus"][k];
                        for(auto &[m,n]:ipcStatus["current"].items()) {
                            n = false;
                        }

                        json data;
                        string msg = fmt::format("evcloudsvc detects cluster mgr {} offline of ipc {}", selfId, k);
                        data["msg"] = msg;
                        data["modId"] = "ALL";
                        data["type"] = EV_MSG_META_TYPE_REPORT;
                        data["catId"] = EV_MSG_REPORT_CATID_AVMGROFFLINE;
                        data["level"] = EV_MSG_META_VALUE_REPORT_LEVEL_ERROR;
                        data["time"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
                        data["status"] = "active";
                        if(ipcStatus["issues"].count(selfId) == 0){
                            ipcStatus["issues"][selfId] = json();
                        }
                        ipcStatus["issues"][selfId][EV_MSG_REPORT_CATID_AVMGROFFLINE] = data;
                        ipcStatus["lastNReports"].push_back(data);
                        if(ipcStatus["lastNReports"].size() > NUM_MAX_REPORT_HISTORY) {
                            ipcStatus["lastNReports"].erase(0);
                        }
                    }
                }  
            }else{
                spdlog::error("{} no such dev {} for disconnect", devSn, selfId);
            }
                    
        }
        return ret;
    }

    // report example
    // data["msg"] = msg;
    // data["modId"] = selfId;
    // data["type"] = EV_MSG_META_TYPE_REPORT;
    // data["catId"] = EV_MSG_REPORT_CATID_AVWRITEPIPE;
    // data["level"] = EV_MSG_META_VALUE_REPORT_LEVEL_ERROR;
    // data["time"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
    // data["status"] = "active";
    void processReportMsg(string peerId, json &data) {
        json modIds;
        if(data["modId"].is_array()) {
            modIds = data["modId"];
        }else if(data["modId"].is_string()) {
            modIds.push_back(data["modId"].get<string>());
        }

        for(const string &modId: modIds){
            if(peerData["mod2ipc"].count(modId) == 0) {
                spdlog::error("{} received report from {} modId {} having no related ipc: {}", devSn, peerId, modId, data.dump());
            }else{
                string ipcSn = peerData["mod2ipc"][modId];
                string status = data["status"];
                string catId = data["catId"];
                string severity = data["level"]; 
                data["modId"] = modId;
                
                if(peerData["ipcStatus"].count(ipcSn) != 0) {
                    auto &ipcStatus = peerData["ipcStatus"][ipcSn];
                    // log report, filter out ping
                    if(catId == EV_MSG_REPORT_CATID_AVMODOFFLINE && status == "recover"){
                        // nop
                    }else{
                        if(ipcStatus.count("lastNReports") == 0){
                            ipcStatus["lastNReports"] = json();
                        }
                        ipcStatus["lastNReports"].push_back(data);
                        if(ipcStatus["lastNReports"].size() > NUM_MAX_REPORT_HISTORY) {
                            ipcStatus["lastNReports"].erase(0);
                        }
                    }

                    // update status
                    if(status == "active") {
                        if(severity == "error") {
                            ipcStatus["current"][modId] = false;
                        }

                        if(ipcStatus["current"][modId] != ipcStatus["expected"][modId]) {
                            if(ipcStatus["issues"].count(modId) == 0){
                                ipcStatus["issues"][modId] = json();
                            }
                            ipcStatus["issues"][modId][catId] = data;
                        }
                    }else{
                        // recover
                        if(ipcStatus["issues"].count(modId) != 0 && 
                            ipcStatus["issues"][modId].count(catId) != 0) {
                            ipcStatus["issues"][modId].erase(catId); 
                            if(ipcStatus["issues"][modId].size() == 0) {
                                ipcStatus["issues"].erase(modId);
                            }   
                        }

                        if(catId == EV_MSG_REPORT_CATID_AVMODOFFLINE || catId == EV_MSG_REPORT_CATID_AVWRITEPIPE || (modId.find("evpuller") != string::npos && catId == EV_MSG_REPORT_CATID_AVOPENINPUT)) {
                            ipcStatus["current"][modId] = true;
                        }
                    }
                }else{
                    spdlog::error("{} can't find ipc for report mod {}", devSn, modId);
                }
            }
        }
    }

    int handleMsg(vector<vector<uint8_t> > &body)
    {
        int ret = 0;
        // ID_SENDER, ID_TARGET, meta ,MSG
        string selfId, peerId, meta;
        if(body.size() == 2 && body[1].size() == 0) {
            selfId = body2str(body[0]);
            bool eventConn = handleConnection(selfId);

            // TODO
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
            spdlog::warn("evcloudsvc dropped an invalid message, size: {}", devSn, body.size());
            return 0;
        }

        // msg to peer
        meta = body2str(body[2]);
        selfId = body2str(body[0]);
        peerId = body2str(body[1]);
        // update online status;
        this->peerData["online"][selfId] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
        int minLen = std::min(body[1].size(), devSn.size());
        if(memcmp((void*)(body[1].data()), devSn.data(), minLen) != 0) {
            // message to other peer
            // check peer online status
            vector<vector<uint8_t> >v = {body[1], body[0], body[2], body[3]};
            if(peerData["online"].count(peerId)!= 0 && peerData["online"][peerId] != 0) {
                spdlog::info("{} route msg from {} to {}", devSn, selfId, peerId);
                ret = z_send_multiple(pRouter, v);
                if(ret < 0) {
                    spdlog::error("{} failed to send multiple: {}", devSn, zmq_strerror(zmq_errno()));
                }
            }
            else {
                // cache
                spdlog::warn("{} cached msg from {} to {}", devSn, selfId, peerId);
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
                spdlog::error("{} exception parse event msg from {} to {}: ", devSn, selfId, peerId, e.what());
            }
        }
        else {
            // message to evcloudsvc
            // spdlog::info("evcloudsvc {} subsystem report msg received: {}; {}; {}", devSn, zmqhelper::body2str(body[0]), zmqhelper::body2str(body[1]), zmqhelper::body2str(body[2]));
            try{
                if(meta == "pong"||meta == "ping") {
                    if(meta=="ping") {
                        auto data = json::parse(body2str(body[3]));
                        spdlog::info("{}, ping msg from {}: {}", devSn, selfId, data.dump());
                        this->peerData["info"]["ips"][selfId] = data["ips"];
                        for(auto &r: data["reports"]) {
                            processReportMsg(selfId, r);
                        }

                        if(cachedMsg.find(selfId) != cachedMsg.end()) {
                            while(!cachedMsg[selfId].empty()) {
                                lock_guard<mutex> lock(cacheLock);
                                auto v = cachedMsg[selfId].front();
                                cachedMsg[selfId].pop();
                                ret = z_send_multiple(pRouter, v);
                                if(ret < 0) {
                                    spdlog::error("{} failed to send multiple: {}", devSn, zmq_strerror(zmq_errno()));
                                }
                            }
                        }
                        else {
                            json resp;
                            resp["metaType"] = EV_MSG_META_PONG;
                            resp["target"] = selfId;
                            sendEdgeMsg(resp);
                        }
                    }
                }
                else {
                    json jmeta = json::parse(meta);
                    if(jmeta["type"] == EV_MSG_META_TYPE_REPORT) {     
                        json data = json::parse(body2str(body[3]));
                        spdlog::warn("{} received report msg from {}: {}", devSn, selfId, data.dump());
                        processReportMsg(selfId, data); 
                    }
                    else {
                        spdlog::warn("{} received unknown msg {} from {}", devSn, meta, selfId);
                    }
                }
            }
            catch(exception &e) {
                spdlog::warn("{} received unknown msg {} from {}", devSn, meta, selfId);
            }
        }

        return ret;
    }

    /// refer to evcloudsvc.yaml
    json getConfigForDevice(string sn)
    {
        json ret;
        ret["code"] = 0;
        ret["msg"] = "ok";
        ret["data"] = json();
        json &data = ret["data"];
        spdlog::info("evcloudsvc get config for sn {}", sn);
        try {
            if(this->configMap["sn2mods"].count(sn) != 0) {
                auto mods = this->configMap["sn2mods"][sn];
                set<string> s;
                for(auto &[k,v]: mods.items()) {
                    s.insert(this->configMap["mod2mgr"][k].get<string>());
                    spdlog::info("evcloudsvc mod2mgr {}->{}", k, this->configMap["mod2mgr"][k].get<string>());
                }

                for(auto &key : s) {
                    if(this->peerData["config"].count(key) == 0) {
                        spdlog::error("evcloudsvc no peerData config for device {}", key);
                    }
                    else {
                        if(data.count(key) != 0) {
                            json diff = json::diff(data[key], this->peerData["config"][key]);
                            if(diff.size() != 0) {
                                string msg = "evcloudsvc inconsistent configuration for k,v, newv: " + key + ",\n" + data[key].dump() + "new v:\n" + this->peerData["config"][key].dump();
                                ret["code"] = 3;
                                ret["msg"] = msg;
                                break;
                            }
                        }
                        else {
                            data[key] = this->peerData["config"][key];
                        }
                    }
                } // for keys of mgr
                ret["data"] = data;
            }
            else {
                ret["code"] = 1;
                string msg = fmt::format("evcloudsvc having no configuration for sn: {}. please POST /config", sn);
                ret["msg"] = msg;
                spdlog::warn(msg);
            }

            auto now = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
            if(peerData["online"].count(sn) != 0 && ((now - peerData["online"][sn].get<decltype(now)>()) < 60) ) {
                ret["online"] = true;
            }
            else {
                ret["online"] = false;
            }
        }
        catch(exception &e) {
            string msg = "evcloudsvc exception in file " + string(__FILE__) + ":" + to_string(__LINE__) + " for: " + e.what();
            spdlog::error(msg);
            ret["code"] = -1;
            ret["msg"] = msg;
        }

        return ret;
    }


    // eventToSlicer["type"] = "event";
    // eventTOSlicer["extraInfo"] = json(); //array
    // eventToSlicer["start"]
    // eventToSlicer["end"]
    // eventToSlicer["sender"] = selfId;

    json sendEdgeMsg(json &body)
    {
        json ret;
        ret["code"] = 0;
        ret["msg"] = "ok";
        string msg;
        try {
            auto target = body["target"].get<string>();
            auto v = strutils::split(target, ':');
            if(v.size() == 1 || v.size() == 3) {
                json meta;
                meta["type"] = body["metaType"];
                if(body.count("metaValue") == 0) {
                    // meta["value"] = "";
                }
                else {
                    meta["value"] = body["metaValue"];
                }

                body["sender"] = devSn;
                if(peerData["online"].count(v[0]) == 0 || peerData["online"][v[0]] == 0) {
                    spdlog::warn("evcloudsvc sent msg {} to {}, but it was offline", body.dump(), v[0]);
                }
                else {
                }
                int i= z_send(pRouter, v[0], devSn, meta, body.dump());
                if(i < 0) {
                    msg = fmt::format("evcloudsvc failed to z_zend msg: {} :{}",zmq_strerror(zmq_errno()),body.dump());
                    throw StrException(msg);
                }
            }
            else {
                msg = fmt::format("evcloudsvc invliad target field({}) in body: {}", target, body.dump());
                throw StrException(msg);
            }

        }
        catch(exception &e) {
            ret["msg"] = e.what();
            spdlog::error(e.what());
            ret["code"] = -1;
        }

        return ret;
    }

    json handleCmd(json &body)
    {
        json ret;
        ret["code"] = -1;
        ret["msg"] = "unkown msg";
        spdlog::info("evcloudsvc handle cmd: {}", body.dump());
        if(body.count("target") != 0 && body["target"].is_string() && body.count("metaType") !=0  && body["metaType"].is_string() &&
                body.count("data") != 0 && body["data"].is_object() && body.count("metaValue") !=0  && body["metaValue"].is_string()) {
            // it's msg to edge.
            return sendEdgeMsg(body);
        }
        else {
            return ret;
        }
    }

    json getReleaseBundle(string bid)
    {
        json ret;
        int stackId = -1;
        if(bid.empty()) {
            ret = this->releaseBundle;
        }
        else {
            try {
                stackId = stoi(bid);
            }
            catch(exception &e) {
                stackId = -1;
            }

            if(this->releaseBundle.size() != 0 && this->releaseBundle.count("bundles") != 0) {
                auto &bunds = this->releaseBundle["bundles"];
                int idx = bunds.size() - 1 - stackId;
                if(stackId >=0 && idx >= 0) {
                    // idx style
                    ret = bunds[idx];
                }
                else {
                    // releaseId style
                    for(auto &r: bunds) {
                        if(r["releaseId"] == bid) {
                            ret = r;
                            break;
                        }
                    }
                }
            }
        }

        return ret;
    }

    string enableRelease(string bid, bool enable)
    {
        string ret;
        int stackId = -1;
        bool handled = false;
        bool isNumber = true;
        if(bid.empty()) {
            if(enable) {
                if(this->releaseBundle.size() > 0) {
                    this->releaseBundle["activeIdx"] = this->releaseBundle.size() - 1;
                    handled = true;
                    // TODO: send release to edge
                }
            }
            else {
                if(this->releaseBundle["bundles"].size() <= 1) {
                    ret = "no release to disable. (maybe only one or none release bundle configured)";
                }
            }
        }
        else {
            try {
                stackId = stoi(bid);
            }
            catch(exception &e) {
                isNumber = false;
            }
            if(this->releaseBundle.size() != 0 && this->releaseBundle.count("bundles") != 0) {
                auto &bunds = this->releaseBundle["bundles"];
                if(isNumber) {
                    int idx = bunds.size() - 1 - stackId;
                    if(idx < 0) {
                        ret = string("no left configure to ") + (enable?"enable":"disable");
                        return ret;
                    }

                    if(this->releaseBundle["activeIdx"] == idx) {
                        if(enable) {
                            spdlog::info("evcloudsvc release {} is already in active. nop.", idx);
                        }
                        else {
                            return enableRelease(to_string(idx - 1), true);
                            // TODO: send release to edge
                        }
                    }
                    else {
                        if(enable) {
                            this->releaseBundle["activeIdx"] = idx;
                            handled = true;
                            // TODO: send release to edge
                        }
                        else {
                            ret = "this release is not in active. nop.";
                        }
                    }
                }
                else {
                    // releaseId style
                    int idx = 0;
                    for(auto &r: bunds) {
                        if(r["releaseId"] == bid) {
                            return enableRelease(to_string(idx), enable);
                        }
                        idx++;
                    }
                }
            }
        }

        return ret;
    }

    string delReleaseBundle(string bid)
    {
        string ret;
        int stackId = -1;
        bool handled = false;
        bool isNumber = true;
        if(bid.empty()) {
            ret = "empty release bundle id";
        }
        else {
            try {
                stackId = stoi(bid);
            }
            catch(exception &e) {
                isNumber = false;
            }

            if(this->releaseBundle.size() != 0 && this->releaseBundle.count("bundles") != 0) {
                auto &bunds = this->releaseBundle["bundles"];
                if(isNumber) {
                    int idx = bunds.size() - 1 - stackId;
                    if(idx < 0) {
                        ret = "no such release config";
                        return ret;
                    }

                    spdlog::info("idx: {}", idx);
                    if(idx == this->releaseBundle["activeIdx"].get<int>()) {
                        ret = "can't delete active release bundle";
                    }
                    else {
                        bunds.erase(idx);
                        if(idx < this->releaseBundle["activeIdx"].get<int>()) {
                            this->releaseBundle["activeIdx"] = this->releaseBundle["activeIdx"].get<int>() -1;
                        }
                        handled = true;
                    }
                }
                else {
                    // releaseId style
                    int idx = 0;
                    for(auto r: bunds) {
                        if(r["releaseId"] == bid) {
                            return delReleaseBundle(to_string(idx));
                        }
                        idx++;
                    }
                }
                if(handled) {
                    // save
                    int r = LVDB::setValue(this->releaseBundle, KEY_RELEASE_BUNDLE);
                    if(r < 0) {
                        string msg = fmt::format("evcloudsvc failed to save release bundle");
                        spdlog::error(msg);
                        ret = msg;
                    }
                }
            }
        }

        return ret;
    }

    string addReleaseBundle(json &bundle)
    {
        string ret;
        if(bundle.count("releaseId") == 0) {
            ret = "no releaseId field";
        }
        else {
            for(auto &b: this->releaseBundle["bundles"]) {
                if(b["releaseId"] == bundle["releaseId"]) {
                    ret = "releaseId already exist: " + b.dump();
                    spdlog::error(string("evcloudsvc POST /release: ") + ret);
                    break;
                }
            }
            if(!ret.empty()) {
                return ret;
            }

            this->releaseBundle["bundles"].push_back(bundle);
            if(bundle.count("active") != 0 && bundle["active"] != 0) {
                // TODO: release to edge
                this->releaseBundle["activeIdx"] = this->releaseBundle["bundles"].size() - 1;
                // sendRealseToEdge(bundle);
            }
            // save
            int r = LVDB::setValue(this->releaseBundle, KEY_RELEASE_BUNDLE);
            if(r < 0) {
                string msg = fmt::format("evcloudsvc failed to save release bundle");
                spdlog::error(msg);
                ret = msg;
            }
        }

        return ret;
    }

    json getClusterInfo(set<string> sns)
    {
        json ret;
        ret["code"] = 0;
        ret["msg"] = "ok";
        ret["data"] = json();

        if(sns.size() == 0) {
            for(auto&[k,v]: configMap["sn2mods"].items()) {
                sns.insert(k);
            }
        }

        for(auto &k: sns) {
            auto conf = getConfigForDevice(k);
            auto now = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();

            if(peerData["online"].count(k) != 0 && ((now - peerData["online"][k].get<decltype(now)>()) < 60) ) {
                conf["online"] = true;
            }
            else {
                conf["online"] = false;
            }
            ret["data"][k] = conf;

        }

        return ret;
    }

protected:
public:
    void run()
    {
        svr.Get("/config", [this](const Request& req, Response& res) {
            json ret;
            ret["code"] = 0;
            ret["time"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
            ret["msg"] = "ok";
            string sn = req.get_param_value("sn");
            // string module = req.get_param_value("module");
            try {
                if(!sn.empty()) {
                    ret = getConfigForDevice(sn);
                }
                else {
                    ret["code"] = 2;
                    ret["msg"] = "invalid request. no param for sn";
                }
            }
            catch(exception &e) {
                ret["code"] = -1;
                ret["msg"] = string("evcloudsvc exception: ") + e.what();
                spdlog::error(ret["msg"].get<string>());
            }

            res.set_content(ret.dump(), "text/json");
        });

        svr.Post("/config", [this](const Request& req, Response& res) {
            json ret;
            string msg;
            try {
                json cfg = json::parse(req.body);
                if(req.has_param("sn") && req.has_param("patch")) {
                    string _sn = req.get_param_value("sn");
                    string _patch = req.get_param_value("patch");
                    spdlog::info("evcloudsvc patch cfg for {}: {}", _sn, cfg.dump());
                    if(!_sn.empty() && _patch == "true") {
                        // verify sn
                        ret = getConfigForDevice(_sn);
                        if(ret["code"] != 0) {
                            spdlog::error("evcloudsvc failed to get confg for {}: {}", _sn, ret["msg"].get<string>());
                        }
                        else {
                            if(ret.count("data") == 0 || ret["data"].size() == 0) {
                                spdlog::error("evcloudsvc no existing valid configuration for {}. abort patching", _sn);
                                ret["msg"] = string("evcloudsvc no existing valid configuration,abort patching for ") + _sn;
                            }
                            else {
                                try {
                                    json patched = ret["data"].patch(cfg);
                                    ret["data"] = patched;
                                    spdlog::info("evcloudsvc merged {}: {} \n\t{}", _sn, cfg.dump(), patched["data"].dump());
                                    ret = this->config(ret);
                                }
                                catch(exception &e) {
                                    string msg = fmt::format("evclousvc exception when patching {} with {}: {}", ret["data"].dump(), cfg.dump(), e.what());
                                    spdlog::error(msg);
                                    ret["code"] = 3;
                                    ret["msg"] = msg;
                                }
                            }
                        }
                    }
                }
                else {
                    spdlog::info("full config: {}", cfg.dump());
                    ret = this->config(cfg);
                }
            }
            catch (exception &e) {
                msg = string("evcloudsvc exception on POST /config: ") +  e.what();
                ret["msg"] = msg;
                ret["code"]= -1;
                spdlog::error(msg);
            }
            res.set_content(ret.dump(), "text/json");
        });


        svr.Get("/ipcstatus", [this](const Request& req, Response& res) {
            json ret;
            ret["code"] = 0;
            ret["time"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
            ret["msg"] = "ok";
            ret["data"] = json();
            json detail, summary;
            string sn = req.get_param_value("sn");
            try {
                if(!sn.empty() && sn != "all") {
                    if(this->peerData["ipcStatus"].count(sn) != 0){
                        json j;
                        j[sn] = this->peerData["ipcStatus"][sn];
                        detail = j;
                    }else{
                        ret["msg"] = "ipc not found";
                        ret["code"] = 1;
                    }
                }else if(sn == "all"){
                    detail = this->peerData["ipcStatus"];   
                }else{
                    // nop
                }
                ret["data"]["detail"] = detail;

                // get a copy to build summary
                json ipcsData = this->peerData["ipcStatus"];
                for(auto &[k,v]: ipcsData.items()){
                    json diff = json::diff(v["expected"], v["current"]);
                    if(diff.size() != 0) {
                        summary["problematic"].push_back(k);
                    }else{
                        summary["ok"].push_back(k);
                    }
                }

                ret["data"]["summary"] = summary;
            }
            catch(exception &e) {
                ret["code"] = -1;
                ret["msg"] = string("evcloudsvc exception: ") + e.what();
                spdlog::error(ret["msg"].get<string>());
            }

            res.set_content(ret.dump(), "text/json");
        });

        svr.Post("/reset", [](const Request& req, Response& res) {

        });

        svr.Post("/cmd", [this](const Request& req, Response& res) {
            json ret;
            string msg;
            ret["code"] = 0;
            ret["msg"] = "ok";
            try {
                auto body = json::parse(req.body);
                ret = this->handleCmd(body);
            }
            catch(exception &e) {
                ret["code"] = -1;
                msg = fmt::format("evcloudsvc Post /cmd Exception: {}", e.what());
                spdlog::error(msg);
                ret["msg"] = msg;
            }

            res.set_content(ret.dump(), "text/json");
        });

        svr.Get("/keys", [](const Request& req, Response& res) {
            string fileName = req.get_param_value("filename");
            auto v = LVDB::getKeys(fileName);
            json ret = v;
            res.set_content(ret.dump(), "text/json");
        });

        svr.Get("/value", [](const Request& req, Response& res) {
            string key = req.get_param_value("key");
            string filename = req.get_param_value("filename");
            json j;
            int ret = LVDB::getValue(j, key, filename);
            if(ret < 0) {
                j["code"] = 1;
                string msg = fmt::format("evcloudsvc failed to get {} in file: {}", key, filename);
                spdlog::error(msg);
                j["msg"] = msg;
            }
            else {
                j["code"] = 0;
                j["msg"] = "ok";
            }
            res.set_content(j.dump(), "text/json");
        });

        svr.Post("/delete", [this](const Request& req, Response& res) {
            string sn = req.get_param_value("sn");
            string filename = req.get_param_value("filename");
            json ret;
            vector<string> mods;
            if(this->configMap.count("sn2mods") != 0 && this->configMap["sn2mods"].size() != 0) {
                for(auto &[k,v]: this->configMap["sn2mods"].items()) {
                    if(k == sn) {
                        for(auto &[a,b]: v.items()) {
                            mods.push_back(a);
                        }

                        this->configMap["sn2mods"].erase(k);
                    }
                }

                if(this->configMap.count("mod2mgr") ==0 || this->configMap["mod2mgr"].size() ==0) {
                }
                else {
                    for(auto &k:mods) {
                        this->configMap["mod2mgr"].erase(k);
                    }
                }

                if(this->configMap.contains(sn))
                    this->configMap.erase(sn);
                if(this->peerData["config"].contains(sn))
                    this->peerData["config"].erase(sn);
                if(this->peerData["online"].contains(sn))
                    this->peerData["online"].erase(sn);

                spdlog::info("evcloudsvc removed sn: {}", sn);
                LVDB::setValue(this->configMap, KEY_CONFIG_MAP);
            }

            res.set_content(this->configMap.dump(), "text/json");
        });

        svr.Get("/release", [this](const Request& req, Response& res) {
            json ret;
            string msg;
            ret["code"] = 0;
            ret["msg"] = "ok";
            try {
                string bundleId = req.get_param_value("bid");
                auto bundle = this->getReleaseBundle(bundleId);
                if(bundle.size() == 0) {
                    ret["code"] = 1;
                    ret["msg"] = "not found";
                }
                else {
                    ret["data"] = bundle;
                }
            }
            catch(exception &e) {
                ret["code"] = -1;
                msg = fmt::format("evcloudsvc Get /release Exception: {}", e.what());
                spdlog::error(msg);
                ret["msg"] = msg;
            }

            res.set_content(ret.dump(), "text/json");
        });

        svr.Post("/release", [this](const Request& req, Response& res) {
            json ret;
            string msg;
            ret["code"] = 0;
            ret["msg"] = "ok";
            try {
                auto body = json::parse(req.body);
                auto s = this->addReleaseBundle(body);
                if(!s.empty()) {
                    ret["code"] = 1;
                    ret["msg"] = s;
                }
            }
            catch(exception &e) {
                ret["code"] = -1;
                msg = fmt::format("evcloudsvc Post /release Exception: {}", e.what());
                spdlog::error(msg);
                ret["msg"] = msg;
            }

            res.set_content(ret.dump(), "text/json");
        });

        svr.Delete("/release", [this](const Request& req, Response& res) {
            json ret;
            string msg;
            ret["code"] = 0;
            ret["msg"] = "ok";
            try {
                string bundleId = req.get_param_value("bid");
                auto s = this->delReleaseBundle(bundleId);
                if(!s.empty()) {
                    ret["msg"] = s;
                    ret["code"] = 1;
                }
            }
            catch(exception &e) {
                ret["code"] = -1;
                msg = fmt::format("evcloudsvc Get /release Exception: {}", e.what());
                spdlog::error(msg);
                ret["msg"] = msg;
            }

            res.set_content(ret.dump(), "text/json");
        });

        svr.listen("0.0.0.0", stoi(httpPort));
    }

    EvCloudSvc()
    {
        int ret = 0;
        this->peerData["info"] = json();
        this->peerData["info"]["ips"] = json();
        this->peerData["config"] = json();
        this->peerData["online"] = json();
        this->peerData["ipcStatus"] = json();
        this->peerData["mod2ipc"] = json();
        this->peerData["mgr2ipc"] = json();
        this->peerData["modulecls"] = json();

        spdlog::info("evcloudsvc boot");
        loadConfigMap();
        char *strEnv = getenv("HTTP_PORT");
        if(strEnv != nullptr) {
            httpPort = strEnv;
        }

        strEnv = getenv("MSG_PORT");
        if(strEnv != nullptr) {
            msgPort = strEnv;
        }

        string addr = "tcp://*:" + msgPort;
        ret = zmqhelper::setupRouter(&pRouterCtx, &pRouter, addr);
        if(ret < 0) {
            spdlog::error("evcloudsvc failed setup router: {}", addr);
            exit(1);
        }

        // setup edge msg processor
        thMsgProcessor = thread([this]() {
            while(true) {
                auto v = zmqhelper::z_recv_multiple(this->pRouter);
                if(v.size() == 0) {
                    spdlog::error("evdaemon {} failed to receive msg {}", this->devSn, zmq_strerror(zmq_errno()));
                }
                else {
                    handleMsg(v);
                }
            }
        });
        thMsgProcessor.detach();

        spdlog::info("evdaemon {} edge message processor had setup {}", devSn, addr);

    };
    ~EvCloudSvc() {};
};

int main()
{
    EvCloudSvc srv;
    srv.run();
}