/*
module: evdaemon
description: to monitor and configure all other components. runs only one instance per host.
author: Bruce.Lu <lzbgt@icloud.com>
update: 2019/08/30
*/

#include "inc/tinythread.hpp"
#include "inc/httplib.h"
#include "inc/zmqhelper.hpp"
#include "inc/database.h"
#include "inc/json.hpp"

using namespace std;
using namespace httplib;
using namespace nlohmann;

class HttpSrv{
    private:
    Server svr;
    json config;

    void setMonitorThread() {

    }

    protected:
    public:
    void run(){
        setMonitorThread();
        // get config
        svr.Get("/config", [](const Request& req, Response& res){
            json rep = R"({"code":0, "msg":"hello"})"_json;
            res.set_content(rep.dump(), "text/json");
        });

        svr.Post("/config", [](const Request& req, Response& res){

        });

        svr.Post("/reset", [](const Request& req, Response& res){
            
        });

        svr.listen("0.0.0.0", 8088);
    }

    HttpSrv(){

    };
    ~HttpSrv(){};
};

int main(){
    HttpSrv srv;
    srv.run();
}