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

using namespace std;
using namespace httplib;
using namespace nlohmann;

//

class HttpSrv{
    private:
    Server svr;
    json configMap;

    protected:
    public:
    void run(){
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
                    //LVDB::setLocalConfig(newConfig);
                    //this->configMap = newConfig;
                    json &data = newConfig["data"];
                    for(auto &[k, v]: data.items()) {
                        if(v.count(k) == 0||v[k].size()==0) {
                            ret["code"] = 2;
                            ret["msg"] = "evcloudsvc invalid value for key " + k;
                            spdlog::error(ret["msg"]);
                        }else{
                            //
                            LVDB::setLocalConfig(v, k);
                            this->configMap[k]=v;
                            LVDB::setValue(this->configMap, "configmap")
                        }
                    }
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