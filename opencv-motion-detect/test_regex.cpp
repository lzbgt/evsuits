#include <iostream>
#include <regex>
#include <iterator>
#include <vector>
#include <string>
#include <set>
#include "json.hpp"
#include <spdlog/spdlog.h>
#include <fmt/format.h>

using namespace std;
using namespace nlohmann;

json getModulesOperFromConfDiff(json& oldConfig, json &newConfig, json &diff, string sn) {
    /// key: gid; value: 0 - stop, 1 - start, 3 - restart
    json ret;
    ret["code"] = 0;
    ret["msg"] = "ok";
    ret["data"] = json();
    bool hasError = false;
    try{
        for(auto &d: diff) {
            if(d.count("path") != 0) {
                string path_ = d["path"];
                bool matched = false;
                // match ipc config
                // eg: /NMXH73Y2/ipcs/0/addr
                // \\w+ could be: addr, user, password, port
                set<string> oprations{"add", "replace", "remove"};
                set<string> pullerTag{"addr", "user", "password", "proto", "port" /*, "sn"*/};

                string ipcRegStr = string("/") + sn + "/ipcs/(\\d+)/(\\w+)";
                std::smatch results;
                std::regex ipcRegex(ipcRegStr);
                if (std::regex_match(path_, results, ipcRegex)) {
                    if (results.size() == 3) {
                        matched = true;
                        int ipcIdx = stoi(results[1].str());
                        string tag = results[2].str();
                        if(pullerTag.find(tag) != pullerTag.end()) {
                            // TODO: op = remove
                            if(d["op"] == "add" || d["op"] == "replace") {
                                // start
                                auto ipc = newConfig[sn]["ipcs"][ipcIdx];
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
                                        if(puller.count("iid") == 0 || puller.count("addr") == 0) {
                                            string msg = fmt::format("invliad config as of having no iid/addr/enabled field in ipc[{}]['modules']['evpuller'][{}]: {}", ipcIdx, idx, newConfig.dump());
                                            spdlog::error(msg);
                                            ret["msg"] = msg;
                                            hasError = true;
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

                // match module config
                if(!matched && !hasError) {
                    // /NMXH73Y2/ipcs/0/modules/evpusher/0/urlDest
                    string  moduleRegStr = string("/") + sn + "/ipcs/(\\d+)/modules/(\\w+)/(\\d+)/(\\w+)";
                    std::regex moduleRegex(moduleRegStr);
                    std::smatch results2;
                    if (std::regex_match(path_, results2, moduleRegex)) {
                        if (results2.size() == 5) {
                            int ipcIdx = stoi(results2[1].str());
                            int modIdx = stoi(results[3].str());
                            string modName = results[2].str();
                            string propName = results[4].str();
                            if(d["op"] == "replace"||d["op"] == "add") {   
                                auto &oldMod = oldConfig[sn]["ipcs"][ipcIdx]["modules"][modIdx];
                                auto &newMod = newConfig[sn]["ipcs"][ipcIdx]["modules"][modIdx];
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

                                    string oldGid = sn + ":" + modName + ":" + oldMod["iid"].get<string>();
                                    string newGid = sn + ":" + modName + ":" + newMod["iid"].get<string>();

                                    if(oldGid != newGid) {
                                        ret["data"][oldGid] = 0;
                                    }

                                    if(propName == "enabled") {
                                        if(newMod["enabled"].get<int>() == 0) {
                                            ret["data"][newGid] = 0;
                                        }else{
                                            ret["data"][newGid] = 2;
                                        }
                                    }else{
                                        // disabled
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
                } 

                if(hasError){
                    ret["code"] = 1;
                }       
            }
        }
    }catch(exception &e) {
        spdlog::error("getModulesOperFromConfDiff exception: {}", e.what());
        ret["code"] = -1;
        ret["msg"] = e.what();
    }

    return ret;
}


int main(){
    // (?:addr|password|port|user)
    string ipcPath = "/NMXH73Y2/ipcs/0/addr";
    string ipcRegStr = string("/NMXH73Y2") + "/ipcs/(\\d+)/(\\w+)";
    std::smatch results;
    std::regex ipcRegex(ipcRegStr);
    if (std::regex_match(ipcPath, results, ipcRegex)) {
        if (results.size() == 3) {
            for(auto &v: results) {
                cout<< v.str() << endl;
            }
        }
    }

    cout << "match2:" << endl;
    string modulePath = "/NMXH73Y2/ipcs/0/modules/evpusher/0/urlDest";
    string  moduleRegStr = "/NMXH73Y2/ipcs/(\\d+)/modules/(\\w+)/(\\d+)/(\\w+)";
    std::regex moduleRegex(moduleRegStr);
    std::smatch results2;
    if (std::regex_match(modulePath, results2, moduleRegex)) {
        if (results2.size() == 5) {
            for(auto &v: results2) {
                cout<< v.str() << endl;
            }
        }
    }

}