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
class EvCloudSvc {
private:
    Server svr;
    void *pRouterCtx = nullptr, *pRouter = nullptr;
    string httpPort = "8089";
    string msgPort = "5548";
    string devSn = "evcloudsvc";

    json configMap;

    // peer data
    json peerData;
    unordered_map<string, queue<vector<vector<uint8_t> >> > cachedMsg;
    mutex cacheLock;
    queue<string> eventQue;
    mutex eventQLock;
    thread thMsgProcessor;

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
    }

    int sendConfig(json &config_, string sn)
    {
        int ret = 0;
        string cfg = config_.dump();
        json j;
        j["type"] = EV_MSG_META_CONFIG;
        string meta = j.dump();
        vector<vector<uint8_t> > v = {str2body(sn), str2body(devSn), str2body(meta), str2body(cfg)};

        // if(peerData["status"].count(sn) == 0||peerData["status"][sn] == 0) {
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
        }else{
            spdlog::info("evcloudsvc config sent to {}: {}", sn, cfg);
        }
        
        //}

        return ret;
    }

    /// v: edge cluster mgr config
    json applyClusterCfg(string k, json& v)
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
                    if(ipc.count("modules") == 0||ipc["modules"].size() == 0) {
                        msg += fmt::format("\tedge cluster {} has no modules for ipc {}", k, ipcIdx);
                        ret["msg"] = msg;
                    }
                    else {
                        json &modules = ipc["modules"];
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
                                if(m.count("sn") == 0 || m["sn"].size() == 0 || m.count("iid") == 0 || m["iid"].size() == 0||(mn == "evml" && (m.count("type") == 0||m["type"].size() == 0))) {
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
                                //ml
                                if(mn == "evml") {
                                    modKey = sn +":evml" + m["type"].get<string>();
                                }
                                else {
                                    modKey = sn + ":" + mn;
                                }

                                // modules
                                if(this->configMap["sn2mods"].count(sn) == 0) {
                                    this->configMap["sn2mods"][sn] = json();
                                }

                                // check exist of modkey
                                if(this->configMap["sn2mods"][sn].count(modKey) == 0) {
                                    this->configMap["sn2mods"][sn][modKey] = 1;
                                }

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
        spdlog::info("evcloudsvc config:{}",newConfig.dump());
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

                    auto r = applyClusterCfg(k,v);
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
        if(peerData["status"].count(selfId) == 0 || peerData["status"][selfId] == 0) {
            peerData["status"][selfId] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
            spdlog::info("evcloudsvc peer connected: {}", selfId);
            ret = true;
            spdlog::debug("evcloudsvc update status of {} to 1 and send config", selfId);
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
        }
        else {
            peerData["status"][selfId] = 0;
            spdlog::warn("{} peer disconnected: {}", devSn, selfId);
        }
        return ret;
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
            spdlog::warn("evcloudsvc {} dropped an invalid message, size: {}", devSn, body.size());
            return 0;
        }

        // msg to peer
        meta = body2str(body[2]);
        selfId = body2str(body[0]);
        peerId = body2str(body[1]);
        // update status;
        this->peerData["status"][selfId] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
        int minLen = std::min(body[1].size(), devSn.size());
        if(memcmp((void*)(body[1].data()), devSn.data(), minLen) != 0) {
            // message to other peer
            // check peer status
            vector<vector<uint8_t> >v = {body[1], body[0], body[2], body[3]};
            if(peerData["status"].count(peerId)!= 0 && peerData["status"][peerId] != 0) {
                spdlog::info("evcloudsvc {} route msg from {} to {}", devSn, selfId, peerId);
                ret = z_send_multiple(pRouter, v);
                if(ret < 0) {
                    spdlog::error("evcloudsvc {} failed to send multiple: {}", devSn, zmq_strerror(zmq_errno()));
                }
            }
            else {
                // cache
                spdlog::warn("evcloudsvc {} cached msg from {} to {}", devSn, selfId, peerId);
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
                spdlog::error("evcloudsvc {} exception parse event msg from {} to {}: ", devSn, selfId, peerId, e.what());
            }
        }
        else {
            // message to evcloudsvc
            // spdlog::info("evcloudsvc {} subsystem report msg received: {}; {}; {}", devSn, zmqhelper::body2str(body[0]), zmqhelper::body2str(body[1]), zmqhelper::body2str(body[2]));
            if(meta == "pong"||meta == "ping") {
                // update status
                spdlog::info("evcloudsvc {}, ping msg from {}", devSn, selfId);
                // handleConnection(selfId);
                if(meta=="ping") {
                    if(cachedMsg.find(selfId) != cachedMsg.end()) {
                        while(!cachedMsg[selfId].empty()) {
                            lock_guard<mutex> lock(cacheLock);
                            auto v = cachedMsg[selfId].front();
                            cachedMsg[selfId].pop();
                            ret = z_send_multiple(pRouter, v);
                            if(ret < 0) {
                                spdlog::error("evcloudsvc {} failed to send multiple: {}", devSn, zmq_strerror(zmq_errno()));
                            }
                        }
                    }else{
                        json resp;
                        resp["metaType"] = EV_MSG_META_PONG;
                        resp["target"] = selfId;
                        sendEdgeMsg(resp);
                    }
                }
            }
            else {
                spdlog::warn("evcloudsvc {} received unknown meta {} from {}", devSn, meta, selfId);
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

    json sendEdgeMsg(json &body) {
        json ret;
        ret["code"] = 0;
        ret["msg"] = "ok";
        string msg;
        try{
            auto target = body["target"].get<string>();
            auto v = strutils::split(target, ':');
            if(v.size() == 1 || v.size() == 3) {
                json meta;
                meta["type"] = body["metaType"];
                if(body.count("metaValue") == 0) {
                    // meta["value"] = "";
                }else{
                    meta["value"] = body["metaValue"];
                }
                
                body["sender"] = devSn;
                if(peerData["status"].count(v[0]) == 0 || peerData["status"][v[0]] == 0){
                    spdlog::warn("evcloudsvc sent msg {} to {}, but it was offline", body.dump(), v[0]);
                }else{
                }
                int i= z_send(pRouter, v[0], devSn, meta, body.dump());
                if(i < 0) {
                    msg = fmt::format("evcloudsvc failed to z_zend msg: {} :{}",zmq_strerror(zmq_errno()) ,body.dump());
                    throw StrException(msg);
                }
            }else{
                msg = fmt::format("evcloudsvc invliad target field({}) in body: {}", target, body.dump());
                throw StrException(msg);
            }

        }catch(exception &e) {
            ret["msg"] = e.what();
            spdlog::error(e.what());
            ret["code"] = -1;
        }

        return ret;
    }

    json handleCmd(json &body){
        json ret;
        ret["code"] = -1;
        ret["msg"] = "unkown msg";
        spdlog::info("evcloudsvc handle cmd: {}", body.dump());
        if(body.count("target") != 0 && body["target"].is_string() && body.count("metaType") !=0  && body["metaType"].is_string() &&
            body.count("data") != 0 && body["data"].is_object() && body.count("metaValue") !=0  && body["metaValue"].is_string()) {
            // it's msg to edge.
            return sendEdgeMsg(body);
        }else{
            return ret;
        }
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
            string module = req.get_param_value("module");
            try {
                if(!sn.empty() && !module.empty() && module.size()> 4) {
                    spdlog::info("evcloudsvc get module config with sn {},  module {}", sn, module);
                    string modname = module.substr(0,4);
                    string key;
                    if(module == "evmgr") {
                        key = sn;
                    }
                    else {
                        if(modname == "evml") {
                            modname = "evml:" + module.substr(4, module.size());
                        }
                        else {
                            modname = module;
                        }

                        key = this->configMap["mod2mgr"].at(sn + ":" + modname);
                        spdlog::debug("key: ", key);
                    }

                    if(!key.empty()) {
                        json config;
                        int iret = LVDB::getLocalConfig(config, key);
                        if(iret < 0) {
                            ret["code"] = 1;
                            ret["msg"] = "evcloud failed to get config with key: " + key ;
                            spdlog::error(ret["msg"].get<string>());
                        }
                        else {
                            ret["data"] = config["data"];
                            ret["lastupdated"] = config["lastupdated"];
                        }
                    }
                    else {
                        ret["code"] = 1;
                        ret["msg"] = "no config for sn " +  sn + ", module " + module;
                    }
                }
                else if(!sn.empty() && module.empty()) {
                    ret = getConfigForDevice(sn);
                }
                else {
                    ret["code"] = 2;
                    ret["msg"] = "invalid request. no param for sn/module";
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

        svr.Post("/reset", [](const Request& req, Response& res) {

        });

        svr.Post("/cmd", [this](const Request& req, Response& res) {
            json ret;
            string msg;
            ret["code"] = 0;
            ret["msg"] = "ok";
            try{
                auto body = json::parse(req.body);
                ret = this->handleCmd(body);
            }catch(exception &e) {
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
                }else{
                    for(auto &k:mods) {
                        this->configMap["mod2mgr"].erase(k);
                    }
                }
                
                this->configMap.erase(sn);
                this->peerData.erase(sn);
                spdlog::info("evcloudsvc removed sn: {}", sn);
                int iret = LVDB::setValue(this->configMap, KEY_CONFIG_MAP);
            }

            res.set_content(this->configMap.dump(), "text/json");
        });

        svr.listen("0.0.0.0", stoi(httpPort));
    }

    EvCloudSvc()
    {
        int ret = 0;
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