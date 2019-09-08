/*
module: evdaemon
description: to monitor and configure all other components. runs only one instance per host.
author: Bruce.Lu <lzbgt@icloud.com>
update: 2019/08/30
*/


#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored  "-Wunused-lambda-capture"

#include <cstdlib>
#include "inc/tinythread.hpp"
#include "inc/httplib.h"
#include "inc/zmqhelper.hpp"
#include "inc/database.h"
#include "inc/json.hpp"
#include "inc/utils.hpp"
#include <unistd.h>

using namespace std;
using namespace httplib;
using namespace nlohmann;

class EvDaemon{
    private:
    Server svr;
    json config;
    json info;
    int port = 8088;
    thread thMon;
    string devSn;
    int portRouter = 5549;
    thread::id thIdMain;

    /// module gid to process id
    json mapModsToPids;

    // for zmq
    void *pRouterCtx = NULL, *pRouter = NULL;

    /// tracking sub-systems: evmgr, evpuller, evpusher, evml*, evslicer etc.
    json mapSubSystems;

    int reloadCfg() {
        int ret = LVDB::getSn(this->info);
        if(ret < 0) {
            spdlog::error("evdaemon {} failed to get info", this->devSn);
            return 1;
        }

        this->devSn = this->info["sn"];
        /// req config
        json jret = cloudutils::reqConfig(this->info);
        spdlog::info("evmgr {} got cloud config:\n{}", devSn, jret.dump(4));
        // apply config
        try{
            if(jret["code"] != 0) {
                spdlog::error("evdaemon {} reqConfig error: {}", this->devSn, jret["msg"].get<string>());
                return 2;
            }

            json &data = jret["data"];
            for(auto &[k,v]:data.items()) {
                if(k == this->devSn) {
                    // startup evmgr
                    pid_t pid = fork();
                    
                    ret = system("./evmgr");
                    if(ret == -1) {
                        spdlog::error("evdaemon {} failed to start evmgr", this->devSn);
                        break;
                    }
                }
            }
        }catch(exception &e) {
            spdlog::error("evdaemon {} exception to reload and apply configuration:\n{}", this->devSn, jret.dump(4));
            return -1;
        }

        return 0;
    }

    void setupSubSystems() {
        thMon = thread([this](){
            while(true) {
                int ret = reloadCfg();
                if(ret != 0) {
                    spdlog::error("evdaemon {} failed to setup subsystems, please check log for more info", this->devSn);
                }
                this_thread::sleep_for(chrono::seconds(5));
            }
        });
    }

    protected:
    public:
    void run(){
        setupSubSystems();

        // get config
        svr.Get("/info", [this](const Request& req, Response& res){
            LVDB::getSn(this->info);
            res.set_content(this->info.dump(), "text/json");
        });

        svr.Post("/info", [this](const Request& req, Response& res){
            json ret;
            ret["code"] = 0;
            ret["msg"] = "ok";
            string sn = req.get_param_value("sn");
            if(sn.empty()){
                ret["code"] = 1;
                ret["msg"] = "no sn in param";
            }else{
                json info;
                info["sn"] = sn;
                // TODO:

                info["lastboot"] =  chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
                LVDB::setSn(info);
            }
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
                
                LVDB::setLocalConfig(newConfig);
                spdlog::info("evmgr new config: {}", newConfig.dump(4));
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

    EvDaemon(){
        char* strPort = getenv("DAEMON_PORT");
        if(strPort != NULL) {
            port = stoi(strPort);
        }

        strPort = getenv("ROUTER_PORT");
        if(strPort != NULL) {
            portRouter = stoi(strPort);
        }

        // setup zmq
        int opt_notify = ZMQ_NOTIFY_DISCONNECT|ZMQ_NOTIFY_CONNECT;
        pRouterCtx = zmq_ctx_new();
        pRouter = zmq_socket(pRouterCtx, ZMQ_ROUTER);
        zmq_setsockopt (pRouter, ZMQ_ROUTER_NOTIFY, &opt_notify, sizeof (opt_notify));
        string addr = "tcp://127.0.0.1:" + to_string(portRouter);
        int ret = zmq_bind(pRouter, addr.c_str());
        if(ret < 0) {
            spdlog::error("evdaemon {} failed to bind port: {}", this->devSn, addr);
            exit(1);
        }
        this->thIdMain = this_thread::get_id();
    };
    ~EvDaemon(){};
};

int main(){
    json info;
    LVDB::getSn(info);
    spdlog::info("evdaemon: \n{}",info.dump(4));
    EvDaemon srv;
    srv.run();
}