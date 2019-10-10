#include <iostream>
#include <regex>
#include <iterator>
#include <vector>
#include <string>
#include <fstream>
#include <set>
#include "json.hpp"
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include "utils.hpp"

using namespace std;
using namespace nlohmann;

json getModulesOperFromConfDiff(json& oldConfig, json &newConfig, json &diff, string sn) {
    /// key: gid; value: 0 - stop, 1 - start, 3 - restart
    json ret;
    ret["code"] = 0;
    ret["msg"] = "ok";
    ret["data"] = json();
    bool hasError = false;
    spdlog::info("matching {}, size:{}, type:{}", diff.dump(), diff.size(), diff.type_name());
    try{
        for(auto &d: diff) {
            spdlog::info("d :{}, {}", d.dump(), d.size());
            if(d.count("path") != 0) {
                string path_ = d["path"];
                bool matched = false;
                // match ipc config
                // eg: /NMXH73Y2/ipcs/0/addr
                // \\w+ could be: addr, user, password, port
                set<string> oprations{"add", "replace", "remove"};
                set<string> pullerTag{"addr", "user", "password", "proto", "port" /*, "sn"*/};

                string ipcRegStr = "/(\\w+)/ipcs/(\\d+)/(\\w+)";
                std::smatch results;
                std::regex ipcRegex(ipcRegStr);
                if (std::regex_match(path_, results, ipcRegex)) {
                    if (results.size() == 4) {
                        matched = true;
                        string mgrSn = results[1].str();
                        int ipcIdx = stoi(results[2].str());
                        string tag = results[3].str();
                        if(pullerTag.find(tag) != pullerTag.end()) {
                            // TODO: op = remove
                            if(d["op"] == "add" || d["op"] == "replace") {
                                // start
                                auto ipc = newConfig[mgrSn]["ipcs"][ipcIdx];
                                auto ipcOld = oldConfig[mgrSn]["ipcs"][ipcIdx];
                                if(ipc.count("modules") == 0 || ipc["modules"].size() == 0 || ipc["moudles"].count("evpuller") ==0 || ipc["modules"]["evpuller"].size() == 0 ) {
                                    string msg = fmt::format("invalid config for ipc[{}]['modules']['evpuller']: {}", ipcIdx, newConfig.dump());
                                    spdlog::error(msg);
                                    ret["msg"] = msg;
                                    hasError = true;
                                    break;
                                }else{
                                    auto &evpullers = ipc["module"]["evpuller"];
                                    int idx = 0;
                                    for(auto &puller:evpullers) {
                                        // strutil
                                        if(puller.count("sn") == 0) {
                                            string msg = fmt::format("invalid config for ipc[{}]['modules']['evpuller'][{}] no sn field: {}", ipcIdx, idx, newConfig.dump());
                                            ret["msg"] = msg;
                                            spdlog::error(msg);
                                            hasError = true;
                                            break;
                                        }

                                        if(puller["sn"].get<string>() != sn) {
                                            spdlog::debug("skip {} for expecting sn: {}", puller.dump(), sn);
                                            continue;
                                        }

                                        if(puller.count("iid") == 0 || puller.count("addr") == 0) {
                                            string msg = fmt::format("invliad config as of having no iid/addr/enabled field in ipc[{}]['modules']['evpuller'][{}]: {}", ipcIdx, idx, newConfig.dump());
                                            spdlog::error(msg);
                                            ret["msg"] = msg;
                                            hasError = true;
                                            break;
                                        }else{
                                            string gid = sn + ":evpuller:" + to_string(puller["iid"].get<int>());
                                            if(puller.count("enabled") == 0 || puller["enabled"].get<int>() == 0) {
                                                ret["data"][gid] = 0; // stop
                                            }else{
                                                ret["data"][gid] = 2;
                                            }
                                        }
                                        idx++;
                                    }
                                }
                            }
                        }
                    }
                }
                // else{
                //     spdlog::info("no match for ipc", path_);
                // }

                // match module config
                if(!matched && !hasError) {
                    // /NMXH73Y2/ipcs/0/modules/evpusher/0/urlDest
                    string  moduleRegStr = "/(\\w+)/ipcs/(\\d+)/modules/(\\w+)/(\\d+)/(\\w+)";
                    std::regex moduleRegex(moduleRegStr);
                    std::smatch results;
                    if (std::regex_match(path_, results, moduleRegex)) {
                        if (results.size() == 6) {
                            matched = true;
                            string mgrSn = results[1].str();
                            int ipcIdx = stoi(results[2].str());
                            int modIdx = stoi(results[4].str());
                            string modName = results[3].str();
                            string propName = results[5].str();
                            if(d["op"] == "replace"||d["op"] == "add" || d["op"] == "remove") {   
                                auto &oldMod = oldConfig[mgrSn]["ipcs"][ipcIdx]["modules"][modName][modIdx];
                                auto &newMod = newConfig[mgrSn]["ipcs"][ipcIdx]["modules"][modName][modIdx];
                                if(oldMod.count("iid") == 0 || newMod.count("iid") == 0) {
                                    string msg = fmt::format("invalid module config ipcs[{}]['modules'][{}][{}] having no iid field", ipcIdx, modName, modIdx);
                                    spdlog::error(msg);
                                    ret["msg"] = msg;
                                    hasError = true;
                                    break;
                                }else{
                                    if(modName == "evml") {
                                        if(newMod.count("type") == 0) {
                                            string msg = fmt::format("invalid evml module config ipcs[{}]['modules'][{}][{}] having no type field", ipcIdx, modName, modIdx);
                                            spdlog::error(msg);
                                            hasError = true;
                                            break;
                                        }else{
                                            modName = modName + newMod["type"].get<string>();
                                        }
                                    }

                                    if(newMod.count("sn") == 0) {
                                        string msg = fmt::format("invalid module config ipcs[{}]['modules'][{}][{}] having no sn field", ipcIdx, modName, modIdx);
                                        spdlog::error(msg);
                                        hasError = true;
                                        break;
                                    }

                                    if(newMod["sn"].get<string>() != sn && oldMod.count("sn") != 0 && oldMod["sn"].get<string>() == sn) {
                                        string oldGid = sn + ":" + modName + ":" + to_string(oldMod["iid"].get<int>());
                                        ret["data"][oldGid] = 0;
                                        continue;
                                    }else if(newMod["sn"].get<string>() != sn && (oldMod.count("sn") == 0 ||(oldMod.count("sn") != 0 && oldMod["sn"].get<string>() != sn))){
                                        // ignore
                                        continue;
                                    }else{
                                        // oldSn == newSn == sn, below
                                    }

                                    string oldGid = sn + ":" + modName + ":" + to_string(oldMod["iid"].get<int>());
                                    string newGid = sn + ":" + modName + ":" + to_string(newMod["iid"].get<int>());

                                    if(oldGid != newGid) {
                                        ret["data"][oldGid] = 0;
                                    }

                                    if(propName == "enabled") {
                                        if(newMod.count("enabled") == 0||newMod["enabled"].get<int>() == 0) {
                                            ret["data"][newGid] = 0;
                                        }else{
                                            ret["data"][newGid] = 1;
                                        }
                                    }else{ // other prop modification
                                        // it was disabled. just ignore
                                        if(ret["data"].count(newGid) != 0 && ret["data"][newGid].get<int>() == 0) {
                                            // nop
                                        }else{
                                            // restart
                                            ret["data"][newGid] = 2;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    // else{
                    //     spdlog::info("no match for module {}", path_);
                    // }
                }

                // whole submodule
                if(!matched && !hasError) {
                    // /PSBV7GKN/ipcs/0/modules/evpusher/0
                    // {"enabled":0,"iid":1,"password":"","sn":"PSBV7GKN","token":"","urlDest":"rtsp://40.73.41.176/PSBV7GKN","user":""}
                    string  moduleRegStr = "/(\\w+)/ipcs/(\\d+)/modules/(\\w+)/(\\d+)";
                    std::regex moduleRegex(moduleRegStr);
                    std::smatch results;
                    if (std::regex_match(path_, results, moduleRegex)) {
                        if (results.size() == 5) {
                            matched = true;
                            string mgrSn = results[1].str();
                            int ipcIdx = stoi(results[2].str());
                            int modIdx = stoi(results[4].str());
                            string modName = results[3].str();
                            json modObj;
                            if(d["op"] == "remove") {
                                modObj = oldConfig[mgrSn]["ipcs"][ipcIdx]["modules"][modName][modIdx];
                            }else{
                                modObj = newConfig[mgrSn]["ipcs"][ipcIdx]["modules"][modName][modIdx];
                            }
                            if(modObj.count("sn") == 0) {
                                if(d["op"] != "remove"){
                                    string msg = fmt::format("invalid modue config having no sn /{}/ipcs/{}/modules/{}/{}", mgrSn, ipcIdx, modName, modIdx);
                                    spdlog::error(msg);
                                    hasError = true;
                                    ret["msg"] = msg;
                                    break;
                                }else{
                                    // nop
                                }
                            }else{
                                if(modObj["sn"].get<string>() == sn){
                                    if(modName == "evml") {
                                        if(modObj.count("type") == 0) {
                                            string msg = fmt::format("invalid evml module config ipcs[{}]['modules'][{}][{}] having no type field", ipcIdx, modName, modIdx);
                                            spdlog::error(msg);
                                            hasError = true;
                                            break;
                                        }else{
                                            modName = modName + modObj["type"].get<string>();
                                        }
                                    }

                                    if(modObj.count("iid") == 0) {
                                        string msg = fmt::format("invalid evml module config ipcs[{}]['modules'][{}][{}] having no iid field", ipcIdx, modName, modIdx);
                                        spdlog::error(msg);
                                        hasError = true;
                                        break;
                                    }

                                    string modGid = sn + ":" + modName + ":" + to_string(modObj["iid"].get<int>());
                                    if(d["op"] == "remove") {
                                        ret["data"][modGid] = 0;
                                    }else{
                                        ret["data"][modGid] = 1;
                                    }
                                }else{
                                    // nop
                                }
                            }
                        }
                    }
                }

                // whole cluster
                if(!matched && !hasError) {
                    // /PSBV7GKN
                    // "value":{"addr":"127.0.0.1","api-cloud":"http://127.0.0.1:8089","ipcs":[{"addr":"172.31.0.129","modules":{"evml":[{"area":200,"enabled":1,"entropy":0.3,"iid":1,"post":30,"pre":3,"sn":"PSBV7GKN","thresh":30,"type":"motion"}],"evpuller":[{"addr":"127.0.0.1","enabled":1,"iid":1,"port-pub":5556,"sn":"PSBV7GKN"}],"evpusher":[{"enabled":0,"iid":1,"password":"","sn":"PSBV7GKN","token":"","urlDest":"rtsp://40.73.41.176/PSBV7GKN","user":""}],"evslicer":[{"enabled":1,"iid":1,"path":"slices","sn":"PSBV7GKN","video-server-addr":"http://40.73.41.176:10009/upload/evtvideos/"}]},"password":"iLabService","port":554,"proto":"rtsp","sn":"iLabService","user":"admin"}],"mqtt-cloud":"<cloud_addr>","port-cloud":5556,"port-router":5550,"proto":"zmq","sn":"PSBV7GKN"}
                    string  clusterRegStr = "/(\\w+)";
                    std::regex clusterRegex(clusterRegStr);
                    std::smatch results;
                    if (std::regex_match(path_, results, clusterRegex)) {
                        if (results.size() == 2) {
                            matched = true;
                            string mgrSn = results[1].str();
                            json mgr;
                            if(d["op"] == "remove"){
                                mgr[mgrSn] = oldConfig[mgrSn];
                            }else{
                                mgr[mgrSn] = newConfig[mgrSn];
                            }

                            json jret = cfgutils::getModuleGidsFromCfg(sn, mgr, "getModulesOperFromConfDiff");
                            spdlog::info("jret: {}", jret.dump());
                            if(jret["code"] != 0) {
                                ret["msg"] = jret["msg"];
                                hasError = true;
                                break;
                            }else{
                                for(auto &k: jret["data"]) {
                                    if(d["op"] == "remove"){
                                        ret["data"][string(k)] = 0;
                                    }else{
                                        ret["data"][string(k)] = 2;
                                    }
                                }
                            }
                        }
                    }
                }

                // one ipc
                if(!matched && !hasError) {
                    // /PSBV7GKN/ipcs/0"
                    // {"addr":"172.31.0.129","modules":{"evml":[{"area":200,"enabled":1,"entropy":0.3,"iid":1,"post":30,"pre":3,"sn":"PSBV7GKN","thresh":30,"type":"motion"}],"evpuller":[{"addr":"127.0.0.1","enabled":1,"iid":1,"port-pub":5556,"sn":"PSBV7GKN"}],"evpusher":[{"enabled":0,"iid":1,"password":"","sn":"PSBV7GKN","token":"","urlDest":"rtsp://40.73.41.176/PSBV7GKN","user":""}],"evslicer":[{"enabled":1,"iid":1,"path":"slices","sn":"PSBV7GKN","video-server-addr":"http://40.73.41.176:10009/upload/evtvideos/"}]},"password":"iLabService","port":554,"proto":"rtsp","sn":"iLabService","user":"admin"}
                    string  clusterRegStr = "/(\\w+)/ipcs/(\\d+)";
                    std::regex clusterRegex(clusterRegStr);
                    std::smatch results;
                    if (std::regex_match(path_, results, clusterRegex)) {
                        if (results.size() == 3) {
                            matched = true;
                            string mgrSn = results[1].str();
                            int ipcIdx = stoi(results[2].str());
                            json mgr;
                            if(d["op"] == "remove"){
                                mgr[mgrSn] = oldConfig[mgrSn];
                            }else{
                                mgr[mgrSn] = newConfig[mgrSn];
                            }

                            json jret = cfgutils::getModuleGidsFromCfg(sn, mgr, "getModulesOperFromConfDiff", ipcIdx);
                            spdlog::info("jret: {}", jret.dump());
                            if(jret["code"] != 0) {
                                ret["msg"] = jret["msg"];
                                hasError = true;
                                break;
                            }else{
                                for(auto &k: jret["data"]) {
                                    if(d["op"] == "remove"){
                                        ret["data"][string(k)] = 0;
                                    }else{
                                        ret["data"][string(k)] = 2;
                                    }
                                }
                            }
                        }
                    }
                } 
            }
        }
        if(hasError){
            ret["code"] = 1;
        }   
    }catch(exception &e) {
        spdlog::error("getModulesOperFromConfDiff exception: {}", e.what());
        ret["code"] = -1;
        ret["msg"] = e.what();
    }

    return ret;
}


int main(){
    std::ifstream iff("deployment/config.json");
    std::ifstream iff2("deployment/config_copy.json");
    json config, config2;
    iff >> config;
    config = config["data"];
    iff2 >> config2;
    config2 = config2["data"];
    json dif = json::diff(config2, config);
    spdlog::info("diff: {}", dif.dump());
    auto ret = getModulesOperFromConfDiff(config2, config, dif, "PSBV7GKN");
    spdlog::info("parse: {}", ret.dump());
}