/*
module: evcloudsvc
description: mqtt & http svc on cloud for evsuits
author: Bruce.Lu <lzbgt@icloud.com>
created: 2019/08/23
update: 2019/09/10
*/

#include <chrono>
#include <set>
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

class HttpSrv {
#define KEY_CONFIG_MAP "configmap"
private:
    Server svr;
    // sn:module -> sn_of_evmgr
    json configMap;

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
            if(newConfig.count("data") == 0 || newConfig["data"].size() == 0) {
                ret["code"] = 1;
                ret["msg"] = "evcloudsvc invalid config body received: " + newConfig.dump(4);
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
                                                this->configMap["sn2mods"][sn].push_back(modKey);

                                                // modkey -> sn_of_evmgr
                                                this->configMap["mod2mgr"][modKey] = k;
                                            }
                                            else {
                                                string msg = "evcloudsvc invalid config: " + data.dump(4);;
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

                    this->configMap[k] = k;

                    //save
                    iret = LVDB::setLocalConfig(evmgrData, k);
                    if(iret < 0) {
                        string msg = "failed to save config " + k + " -> " + evmgrData.dump(4);
                        spdlog::error(msg);
                        ret["code"] = iret;
                        ret["msg"] = msg;
                    }
                } // for evmgr

                // save configmap
                if(ret["code"] == 0) {
                    iret = LVDB::setValue(this->configMap, KEY_CONFIG_MAP);
                    if(iret >= 0) {
                    }
                    else {
                        ret["code"] = iret;
                        ret["msg"] = "failed to save configmap";
                    }
                }
                else {
                    this->configMap = oldConfigMap;
                }

                ret["data"] = newConfig["data"];
            }
        }
        catch(exception &e) {
            ret.clear();
            ret["code"] = -1;
            ret["msg"] = e.what();
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

        // svr.Post("/register", [this](const Request& req, Response& res){
        //     json ret;
        //     try{
        //         string sn = req.get_param_value("sn");
        //         string module = req.get_param_value("module");
        //         bool force = (req.get_param_value("force") == "true") ? true: false;

        //         if(sn.empty()||module.empty()){
        //             throw StrException("no para sn/module");
        //         }

        //         auto cfg = json::parse(req.body);
        //         string key, modname;
        //         if(module == "evmgr") {
        //             key = sn;
        //             // trigger exception
        //             (void)cfg.at("data").at(key);
        //         }else {
        //             if(modname == "evml") {
        //                 string
        //                 modname = "evml:" + module.substr(4, module.size());

        //             }else{
        //                 modname = module;
        //             }
        //             modname = sn + ":" + modname;
        //             if(this->configMap.count(modname) == 0){
        //                 spdlog::info("evcloudsvc no such edge module registred: {}, create new entry", key);
        //                 ret = this->config(cfg);
        //                 if(ret["code"] == 0) {
        //                 }else{
        //                     spdlog::error("failed to config: {}", ret.dump(4));
        //                 }
        //             }else{
        //                 key = configMap[modname];
        //             }
        //         }
        //         if(!key.empty()){
        //             // TODO: calc md5
        //             spdlog::info("evcloudsvc key: {}", key);
        //             int r;
        //             ret["code"] = 0;
        //             ret["msg"] = "diff";
        //             json data;
        //             r = LVDB::getLocalConfig(data, key);
        //             if(r < 0||force) {
        //                 spdlog::error("failed to get localconfig or force to updaste. create new");
        //                 ret = this->config(cfg);
        //             }else{
        //                 json diff = json::diff(cfg, data);
        //                 spdlog::info("evcloudsvc diff: {}", diff.dump(4));
        //                 ret["data"] = diff;
        //             }
        //         }
        //     }catch(exception &e) {
        //         ret.clear();
        //         ret["code"] = -1;
        //         ret["msg"] = e.what();
        //     }

        //     res.set_content(ret.dump(), "text/json");

        // });

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
                    spdlog::info("evcloudsvc get config for sn {}", sn);
                    if(this->configMap["sn2mods"].count(sn) != 0) {
                        auto mods = this->configMap["sn2mods"][sn];
                        set<string> s;
                        for(const string & elem : mods) {
                            s.insert(this->configMap["mod2mgr"][elem].get<string>());
                            spdlog::info("evcloudsvc {}->{}", elem, this->configMap["mod2mgr"][elem].get<string>());
                        }

                        json data;

                        for(auto &key : s) {
                            json cfg;
                            int iret = LVDB::getLocalConfig(cfg, key);
                            if(iret < 0) {
                                ret["code"] = 1;
                                ret["msg"] = "evcloud failed to get config with key: " + key ;
                                spdlog::error(ret["msg"].get<string>());
                            }
                            else {
                                for(auto &[k,v]: cfg["data"].items()) {
                                    if(data.count(k) != 0) {
                                        json diff = json::diff(data[k], v);
                                        if(diff.size() != 0) {
                                            string msg = "evcloudsvc inconsistent configuration for k,v, newv: " + k + ",\n" + data[k].dump(4) + "new v:\n" + v.dump(4);
                                            ret["code"] = 3;
                                            ret["msg"] = msg;
                                            break;
                                        }
                                    }
                                    else {
                                        data[k] = v;
                                    }
                                } // for each mgr

                                if(ret["code"] != 0) {
                                    break;
                                }
                            }
                        } // for keys of mgr
                        ret["data"] = data;
                    }else{
                        ret["code"] = 1;
                        string msg = "no such sn: " + sn;
                        ret["msg"] = msg;
                        spdlog::warn("evcloudsvc no config for sn: {}", sn);   
                    }
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

        svr.listen("0.0.0.0", 8089);
    }

    HttpSrv()
    {

    };
    ~HttpSrv() {};
};

int main()
{
    HttpSrv srv;
    srv.run();
}