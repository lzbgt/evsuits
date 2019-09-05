/*
module: evdaemon
description: to monitor and configure all other components. runs only one instance per host.
author: Bruce.Lu <lzbgt@icloud.com>
update: 2019/08/30
*/


#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored  "-Wunused-lambda-capture"

#include "inc/tinythread.hpp"
#include "inc/httplib.h"
#include "inc/zmqhelper.hpp"
#include "inc/database.h"
#include "inc/json.hpp"
#include <cstdlib>

using namespace std;
using namespace httplib;
using namespace nlohmann;

class HttpSrv{
    private:
    Server svr;
    json config;
    json info;
    int port = 8088;

    void setMonitorThread() {

    }

    protected:
    public:
    void run(){
        setMonitorThread();
        // get config
        svr.Get("/info", [this](const Request& req, Response& res){
            LVDB::getSn(this->info);
            res.set_content(this->info.dump(), "text/json");
        });

        svr.Get("/config", [this](const Request& req, Response& res){
            LVDB::getLocalConfig(this->config);
            res.set_content(this->config.dump(), "text/json");
        });

        svr.Post("/config", [this](const Request& req, Response& res){
            json ret;
            ret["code"] = 0;
            ret["msg"] = "ok";
            ret["time"] = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
            try{
                json newConfig;
                newConfig["data"] = json::parse(req.body)["data"];
                newConfig["lastupdated"] = ret["time"];
                
                LVDB::setLocalConfig(newConfig);
                spdlog::info("evmgr new config: {}", newConfig.dump());
                // TODO: restart other components
                //
            }catch(exception &e) {
                ret.clear();
                ret["code"] = -1;
                ret["msg"] = e.what();
                ret["data"] = req.body;
            }
            res.set_content(ret.dump(), "text/json");
        });

        svr.Post("/reset", [](const Request& req, Response& res){
            
        });

        svr.listen("0.0.0.0", 8088);
    }

    HttpSrv(){
        char* strPort = getenv("PORT");
        if(strPort != NULL) {
            port = stoi(strPort);
        }
    };
    ~HttpSrv(){};
};

int main(){
    json info;
    LVDB::getSn(info);
    spdlog::info("evdaemon: {}",info.dump());
    HttpSrv srv;
    srv.run();
}