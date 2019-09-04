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

using namespace std;
using namespace httplib;
using namespace nlohmann;

//

class HttpSrv{
    private:
    Server svr;
    // sn:module -> sn_of_evmgr
    json configMap;

    protected:
    public:
    void run(){
        // load configmap
        json cnfm;
        LVDB::getValue(cnfm, "configmap");
        if(cnfm.size != 0){
            this->configMap = cnfm;
        }

        svr.Get("/config", [this](const Request& req, Response& res){
            json ret;
            ret["code"] = 0;
            ret["time"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
            ret["msg"] = "ok";
            if(!req.has_param("sn") || !req.has_param("module")){
                ret["code"] = 1;
                ret["msg"] = "evcloud bad req: no sn/module param";
                spdlog::error("evcloud bad req: {}", req);
            }else{
                string sn = req.get_param_value("sn");
                string module = req.get_param_value("module");
                string key = sn + ":" + module;
                if(this->configMap.count(key) != 0) {
                    json config;
                    ret = LVDB::getLocalConfig(config, this->configMap[key]);

                    if(ret < 0) {
                        ret["code"] = 1;
                        ret["msg"] = "evcloud failed to get config with k, v:" + key + " " + this->configMap[key].get<string>();
                        spdlog::error(ret["msg"]);
                    }else{
                        ret["data"] = config;
                    }
                }else{
                    ret["code"] = 1;
                    ret["msg"] = "no config for sn " +  sn, + ", module " + module;
                }  
            }

            res.set_content(ret.dump(), "text/json");
        });

        svr.Post("/config", [this](const Request& req, Response& res){
            json ret;
            ret["time"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
            ret["code"] = 0;
            ret["msg"] = "ok";
            try{
                json newConfig = json::parse(req.body);
                if(newConfig.count("data") == 0 || newConfig["data"].size() == 0) {
                    ret["code"] = 1;
                    ret["msg"] = "evcloudsvc invalid config body received: " + req.body;
                    spdlog::error(ret["msg"]);
                }else{
                    json &data = newConfig["data"];
                    for(auto &[k, v]: data.items()) {
                        // this is one evmgr
                        if(v.count(k) == 0||v[k].size()==0) {
                            ret["code"] = 2;
                            ret["msg"] = "evcloudsvc invalid value for key " + k;
                            spdlog::error(ret["msg"]);
                            continue;
                        }else{
                            // find all modules
                            if(v.count("ipcs") == 0||v["ipcs"].size() == 0) {
                                spdlog::error("invalid ipcs in config body");
                                continue;
                            }else{
                                json &ipcs = v["ipcs"];
                                for(auto &ipc : ipcs) {
                                    if(ipc.count("modules") == 0||ipc["modules"].size() == 0) {
                                        spdlog::error("invalid modules in ipcs config body");
                                        continue;
                                    }else{
                                        json &modules = ipc["modules"];
                                        for(auto &[mn, ml]: modules.items()) {
                                            if(ml.count("sn") != 0 && ml["sn"].size() != 0){
                                                string modKey;
                                                //ml
                                                if(mn == "evml" && ml.count("type") != 0 && ml["type"].size() != 0) {
                                                    modKey = ml["sn"] + ":evml:" + ml["type"];
                                                }else{
                                                    modKey = ml["sn"] + ":" + mn;
                                                }
                                                this->configMap[modKey] = v;
                                            }
                                        } // for modules
                                    }
                                } // for ipc
                            }  
                        }
                        // update
                        auto lastupdated = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
                        json evmgrData;
                        v["lastupdated"] = lastupdated;
                        evmgrData[k] = v;
                        //save
                        LVDB::setLocalConfig(evmgrData, k);
                    } // for evmgr

                    // save configmap
                    LVDB::setValue(this->configMap, "configmap");
                }
                

                // TODO: restart other components
                //
            }catch(exception &e) {
                ret.clear();
                ret["code"] = 1;
                ret["msg"] = e.what();
            }
            res.set_content(ret.dump(), "text/json");
        });

        svr.Post("/reset", [](const Request& req, Response& res){
            
        });

        svr.listen("0.0.0.0", 8088);
    }

    HttpSrv(){

    };
    ~HttpSrv(){};
};

int main() {

}