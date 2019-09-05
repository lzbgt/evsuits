/*
module: evcloudsvc
description: mqtt & http svc on cloud for evsuits
author: Bruce.Lu <lzbgt@icloud.com>
update: 2019/09/02
*/

#include <chrono>
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

//

class HttpSrv{
    #define KEY_CONFIG_MAP "configmap"
    private:
    Server svr;
    // sn:module -> sn_of_evmgr
    json configMap;

    json config(string body){
        json ret;
        int iret;
        json oldConfigMap = this->configMap;
        ret["code"] = 0;
        ret["msg"] = "ok";
        ret["time"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
        spdlog::info(body);
        if(body.empty()){
            ret["code"] = 1;
            ret["msg"] = "no body payload";
        }else{
           try{
                json newConfig = json::parse(body);
                if(newConfig.count("data") == 0 || newConfig["data"].size() == 0) {
                    ret["code"] = 1;
                    ret["msg"] = "evcloudsvc invalid config body received: " + body;
                    spdlog::error(ret["msg"].get<string>());
                }else{
                    json &data = newConfig["data"];
                    for(auto &[k, v]: data.items()) {
                        // this is one evmgr
                        if(v.count("sn") == 0||v["sn"] != k) {
                            ret["code"] = 2;
                            ret["msg"] = "evcloudsvc invalid value for key " + k;
                            spdlog::error(ret["msg"].get<string>());
                            break;
                        }else{
                            // find all modules
                            if(v.count("ipcs") == 0||v["ipcs"].size() == 0) {
                                spdlog::error("invalid ipcs in config body");
                                ret["code"] = 3;
                                break;
                            }else{
                                json &ipcs = v["ipcs"];
                                for(auto &ipc : ipcs) {
                                    if(ipc.count("modules") == 0||ipc["modules"].size() == 0) {
                                        spdlog::error("invalid modules in ipcs config body");
                                        ret["code"] = 4;
                                        break;
                                    }else{
                                        json &modules = ipc["modules"];
                                        for(auto &[mn, ma]: modules.items()) {
                                            for(auto &m:ma) {
                                                if(m.count("sn") != 0 && m["sn"].size() != 0){
                                                    string modKey;
                                                    //ml
                                                    if(mn == "evml" && m.count("type") != 0 && m["type"].size() != 0) {
                                                        modKey = m["sn"].get<string>() +":evml:" + m["type"].get<string>();
                                                    }else{
                                                        modKey = m["sn"].get<string>() + ":" + mn;
                                                    }
                                                    // modkey -> sn_of_evmgr
                                                    this->configMap[modKey] = k;
                                                }else{
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
                        json evmgrData;
                        evmgrData["data"] = data;
                        //evmgrData["lastupdated"] = newConfig["lastupdated"];

                        //save
                        iret = LVDB::setLocalConfig(evmgrData, k);
                        if(iret < 0) {
                            string msg = "failed to save config " + k + " -> " + evmgrData.dump();
                            spdlog::error(msg);
                            ret["code"] = iret;
                            ret["msg"] = msg;
                        }
                    } // for evmgr

                    // save configmap
                    if(ret["code"] == 0) {
                        iret = LVDB::setValue(this->configMap, KEY_CONFIG_MAP);
                        if(iret >= 0) {
                        }else{
                            ret["code"] = iret;
                            ret["msg"] = "failed to save configmap";
                        }
                    }else{
                        this->configMap = oldConfigMap;
                    }

                    ret["data"] = newConfig["data"];   
                }
            }catch(exception &e) {
                ret.clear();
                ret["code"] = -1;
                ret["msg"] = e.what();
            }
        }
        
        return ret;
    }

    protected:
    public:
    void run(){
        // load configmap
        json cnfm;
        LVDB::getValue(cnfm, KEY_CONFIG_MAP);
        if(cnfm.size() != 0){
            this->configMap = cnfm;
        }

        svr.Post("/register", [this](const Request& req, Response& res){
            json ret;
            try{
                string sn = req.get_param_value("sn");
                string module = req.get_param_value("module");
                if(sn.empty()||module.empty()){
                    throw StrException("no para sn/module");
                }
                string modname = module.substr(0,4);
                if(modname == "evml") {
                    modname = "evml:" + module.substr(4, module.size());
                }else{
                    modname = module;
                }

                string key = sn + ":" + modname;

                if(this->configMap.count(key) == 0){
                    //
                    spdlog::info("evcloudsvc no such edge module registred: {}, create new entry", key);
                    ret = this->config(req.body);
                    if(ret["code"] == 0) {
                        //ret["data"] =ret["data"];
                    }else{
                        spdlog::error("failed to config: {}", ret.dump());
                    }
                }else{
                    // TODO: calc md5
                    int r;
                    ret["code"] = 0;
                    ret["msg"] = "ok";
                    string dk = this->configMap[key];
                    json data;
                    r = LVDB::getLocalConfig(data, dk);
                    if(ret < 0) {
                        ret["code"] = r;
                        ret["msg"] = "failed to get config for: " + dk;
                    }else{
                        ret["data"] = data;
                    }
                }

            }catch(exception &e) {
                ret.clear();
                ret["code"] = -1;
                ret["msg"] = e.what();
            }

            res.set_content(ret.dump(), "text/json");
            
        });

        svr.Get("/config", [this](const Request& req, Response& res){
            json ret;
            ret["code"] = 0;
            ret["time"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
            ret["msg"] = "ok";
            string sn = req.get_param_value("sn");
            string module = req.get_param_value("module");
            if(sn.empty() || module.empty() || module.size()< 4){
                ret["code"] = 1;
                ret["msg"] = "evcloud bad req: no sn/module param";
                spdlog::error(ret["msg"].get<string>());
            }else{
                string modname = module.substr(0,4);
                string key;
                if(module == "evmgr") {
                    key = sn;
                }else {
                    if(modname == "evml") {
                        modname = "evml:" + module.substr(4, module.size());
                    }else{
                        modname = module;
                    }
                    key = sn + ":" + modname;
                }
                
                if(!key.empty()) {
                    json config;
                    int iret = LVDB::getLocalConfig(config, key);
                    if(iret < 0) {
                        ret["code"] = 1;
                        ret["msg"] = "evcloud failed to get config with k, v:" + key + " " + this->configMap[key].get<string>();
                        spdlog::error(ret["msg"].get<string>());
                    }else{
                        ret["data"] = config["data"];
                        ret["lastupdated"] = config["lastupdated"];
                    }
                }else{
                    ret["code"] = 1;
                    ret["msg"] = "no config for sn " +  sn + ", module " + module;
                }  
            }

            res.set_content(ret.dump(), "text/json");
        });

        svr.Post("/config", [this](const Request& req, Response& res){
            auto ret = this->config(req.body);
            res.set_content(ret.dump(), "text/json");
        });

        svr.Post("/reset", [](const Request& req, Response& res){
            
        });

        svr.listen("0.0.0.0", 8089);
    }

    HttpSrv(){

    };
    ~HttpSrv(){};
};

int main() {
    HttpSrv srv;
    srv.run();
}