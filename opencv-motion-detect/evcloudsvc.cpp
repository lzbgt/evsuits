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

    int sendConfig(json &config_, string sn) {
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
            spdlog::info("evcloudsvc config sent to {}: {}", sn, cfg);
        //}
        
        return ret;
    }

    json config(json &newConfig)
    {
        json ret;
        int iret;
        json oldConfigMap = this->configMap;
        ret["code"] = 0;
        ret["msg"] = "ok";
        ret["time"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
        spdlog::info(newConfig.dump());
        try {
            json deltaCfg = json();
            if(newConfig.count("data") == 0 || newConfig["data"].size() == 0) {
                ret["code"] = 1;
                ret["msg"] = "evcloudsvc invalid config body received: " + newConfig.dump();
                spdlog::error(ret["msg"].get<string>());
            }
            else {
                json &data = newConfig["data"];

                for(auto &[k, v]: data.items()) {
                    // TODO: confirm overwrite, take snapshot
                    if(this->configMap.count(k) != 0) {
                        spdlog::warn("evcloudsvc TODO: key {} already exist, take snapshot for safety", k);
                    }

                    // this is one evmgr
                    if(v.count("sn") == 0||v["sn"] != k) {
                        ret["code"] = 2;
                        ret["msg"] = "evcloudsvc invalid value for key " + k;
                        spdlog::error(ret["msg"].get<string>());
                        break;
                    }
                    else {
                        // find all modules
                        if(v.count("ipcs") == 0||v["ipcs"].size() == 0) {
                            spdlog::error("invalid ipcs in config body");
                            ret["code"] = 3;
                            break;
                        }
                        else {
                            json &ipcs = v["ipcs"];
                            for(auto &ipc : ipcs) {
                                if(ipc.count("modules") == 0||ipc["modules"].size() == 0) {
                                    spdlog::error("invalid modules in ipcs config body");
                                    ret["code"] = 4;
                                    break;
                                }
                                else {
                                    json &modules = ipc["modules"];

                                    for(auto &[mn, ma]: modules.items()) {
                                        for(auto &m:ma) {
                                            if(m.count("sn") != 0 && m["sn"].size() != 0) {
                                                string modKey;
                                                string sn = m["sn"];
                                                //ml
                                                if(mn == "evml" && m.count("type") != 0 && m["type"].size() != 0) {
                                                    modKey = sn +":evml:" + m["type"].get<string>();
                                                }
                                                else {
                                                    modKey = sn + ":" + mn;
                                                }

                                                // modules
                                                if(this->configMap["sn2mods"].count(sn) == 0) {
                                                    this->configMap["sn2mods"][sn] = json();
                                                }
                                                // check exist
                                                bool hasModKey =false;
                                                for(auto &modKey_:this->configMap["sn2mods"][sn]){
                                                    if(modKey_ == modKey) {
                                                        hasModKey = true;
                                                        break;
                                                    }
                                                }

                                                if(hasModKey){
                                                    //nop
                                                }else{
                                                    this->configMap["sn2mods"][sn].push_back(modKey);
                                                }
                                                
                                                // modkey -> sn_of_evmgr
                                                this->configMap["mod2mgr"][modKey] = k;
                                            }
                                            else {
                                                string msg = "evcloudsvc invalid config: " + data.dump();;
                                                ret["code"] = -1;
                                                ret["msg"] = msg;
                                                spdlog::error(msg);
                                                break;
                                            }
                                        }
                                    } // for modules

                                    if(ret["code"] != 0) {
                                        break;
                                    }
                                } // for ipc
                            }
                        }

                        if(ret["code"] != 0) {
                            break;
                        }
                    }
                    // update evmgr config
                    this->configMap[k] = k;

                    //save
                    iret = LVDB::setLocalConfig(v, k);
                    if(iret < 0) {
                        string msg = "failed to save config " + k + " -> " + v.dump();
                        spdlog::error(msg);
                        ret["code"] = iret;
                        ret["msg"] = msg;
                    }

                    // update in memory peerData
                    if(this->peerData["config"].count(k) != 0) {
                        json diff = json::diff(this->peerData["config"][k], v);
                        if(diff.size()!=0) {
                            // send config
                            deltaCfg[k] = 1;
                            this->peerData["config"][k] = v;
                            spdlog::info("evcloudsvc peer {} config diff:\n{}\norigin:\n{}", k, diff.dump(), this->peerData["config"][k].dump());
                        }else{
                            spdlog::info("evcloudsvc peer {} config no diff. ignored:\n{}", k, this->peerData["config"][k].dump());
                        }
                    }else{
                        this->peerData["config"][k] = v;
                    }
                    
                    // TODO: trigger msg
                } // for evmgr

                // save configmap
                if(ret["code"] == 0) {
                    iret = LVDB::setValue(this->configMap, KEY_CONFIG_MAP);
                    if(iret >= 0) {
                    }
                    else {
                        spdlog::error("evcloudsvc failed to parse and save new config");
                        ret["code"] = iret;
                        ret["msg"] = "failed to save configmap";
                    }
                }
                else {
                    this->configMap = oldConfigMap;
                }

                // update config
                for(auto &[x,y]: deltaCfg.items()){
                    json j = getConfigForDevice(x);
                    if(j["code"] == 0) {
                        sendConfig(j["data"], x);
                    }
                }

                ret["data"] = newConfig["data"];
            }
        }
        catch(exception &e) {
            ret.clear();
            ret["code"] = -1;
            ret["msg"] = string("evcloudsvc exception: ") + e.what();
        }


        return ret;
    }

    // 
    bool handleConnection(string selfId) {
        bool ret = false;
        int state = zmq_socket_get_peer_state(pRouter, selfId.data(), selfId.size());
        spdlog::info("{} state: {}", selfId, state);
        if(peerData["status"].count(selfId) == 0 || peerData["status"][selfId] == 0) {
                peerData["status"][selfId] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
                spdlog::info("evcloudsvc peer connected: {}", selfId);
                ret = true;
                spdlog::debug("evcloudsvc update status of {} to 1 and send config", selfId);
                json data = getConfigForDevice(selfId);
                if(data["code"] != 0) {
                    //
                }else{
                    sendConfig(data["data"], selfId);
                }
            }
            else {
                peerData["status"][selfId] = 0;
                spdlog::warn("evcloudsvc {} peer disconnected: {}", devSn, selfId);
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
                handleConnection(selfId);
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
                    }
                }
            }
            else {
                // TODO:
                spdlog::warn("evcloudsvc {} received unknown meta {} from {}", devSn, meta, selfId);
            }
        }

        return ret;
    }

    json getConfigForDevice(string sn) {
        json ret;
        ret["code"] = 0;
        ret["msg"] = "ok";
        ret["data"] = json();
        json &data = ret["data"];
        spdlog::info("evcloudsvc get config for sn {}", sn);
        try{
            if(this->configMap["sn2mods"].count(sn) != 0) {
                auto mods = this->configMap["sn2mods"][sn];
                set<string> s;
                for(const string & elem : mods) {
                    s.insert(this->configMap["mod2mgr"][elem].get<string>());
                    spdlog::info("evcloudsvc {}->{}", elem, this->configMap["mod2mgr"][elem].get<string>());
                }

                for(auto &key : s) {
                    if(this->peerData["config"].count(key) == 0) {
                        spdlog::error("evcloudsvc no peerData config for device {}", key);
                    }else{
                        if(data.count(key) != 0) {
                            json diff = json::diff(data[key], this->peerData["config"][key]);
                            if(diff.size() != 0) {
                                string msg = "evcloudsvc inconsistent configuration for k,v, newv: " + key + ",\n" + data[key].dump() + "new v:\n" + this->peerData["config"][key].dump();
                                ret["code"] = 3;
                                ret["msg"] = msg;
                                break;
                            }
                        }else{
                            data[key] = this->peerData["config"][key];
                        }
                    } 
                } // for keys of mgr
                ret["data"] = data;
            }else{
                ret["code"] = 1;
                string msg = "no such sn: " + sn;
                ret["msg"] = msg;
                spdlog::warn("evcloudsvc no config for sn: {}", sn);
                // TODO: append to retry queue 
            }
        }catch(exception &e) {
            string msg = "evcloudsvc exception in file" + string(__FILE__) + ":" + to_string(__LINE__) + " for: " + e.what();
            spdlog::error(msg);
            ret["code"] = -1;
            ret["msg"] = msg;
        }
        
        return ret;
    }

protected:
public:
    void run()
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
        for(auto &[k,v]: this->configMap["sn2mods"].items()){
            // load config from database
            json cfg;
            if(LVDB::getLocalConfig(cfg, k) < 0) {
                spdlog::error("evcloudsvc failed to load config for device: {}", k);
            }else{
                this->peerData["config"][k] = cfg;
                spdlog::info("evcloudsvc loaded config for device: {}", k);
            }
        }

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
                }else{
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
                ret = this->config(cfg);
            }
            catch (exception &e) {
                msg = string("evcloudsvc exception on POST /config: ") +  e.what();
                ret["msg"] = msg;
                ret["code"]= -1;
            }
            res.set_content(ret.dump(), "text/json");
        });

        svr.Post("/reset", [](const Request& req, Response& res) {

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
            }
            else {
                j["code"] = 0;
            }
            res.set_content(j.dump(), "text/json");
        });

        svr.listen("0.0.0.0", stoi(httpPort));
    }

    EvCloudSvc()
    {
        int ret = 0;
        spdlog::info("evcloudsvc boot");
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
        thMsgProcessor = thread([this](){
            while(true){
                auto v = zmqhelper::z_recv_multiple(this->pRouter);
                if(v.size() == 0) {
                    spdlog::error("evdaemon {} failed to receive msg {}", this->devSn, zmq_strerror(zmq_errno()));
                }else{
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