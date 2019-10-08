#include <iostream>
#include <regex>
#include <iterator>
#include <vector>
#include <string>
#include <set>
#include "inc/json.hpp"

using namespace std;
using namespace nlohmann;

json getModuleActionFromDiff(json& oldConfig, json &newConfig, json &diff, string sn) {
    json ret;
    for(auto &d: diff) {
        if(d.count("path") != 0) {
            string path_ = d["path"];
            bool matched = false;
            // match ipc config
            // eg: /NMXH73Y2/ipcs/0/addr
            // \\w+ could be: addr, user, password, port
            set<string> oprations{"add", "replace", "remove"};
            set<string> pullerTag{"addr", "user", "password", "proto", "port" /*, "sn"*/};

            string ipcRegStr = string("/") + this->devSn + "/ipcs/(\\d+)/(\\w+)";
            std::smatch results;
            std::regex ipcRegex(ipcRegStr);
            if (std::regex_match(path_, results, ipcRegex)) {
                if (results.size() == 3) {
                    matched = true;
                    int ipcIdx = stoi(results[1].str());
                    string tag = results[2].str();
                    if(pullerTag.find(tag) != pullerTag.end()) {
                        if(d["op"] == "add") {
                            // start
                            auto ipc = newConfig[sn]["ipcs"][ipcIdx];
                            if(ipc.count("modules") == 0 || ipc["modules"].size() == 0 || ipc["moudles"].count("evpuller") ==0 || ipc["modules"] )
                        }
                    }
                }
            }

            // match module config
            if(!matched) {
                // /NMXH73Y2/ipcs/0/modules/evpusher/0/urlDest
                string  moduleRegStr = "/NMXH73Y2/ipcs/(\\d+)/modules/(\\w+)/(\\d+)/(\\w+)";
                std::regex moduleRegex(moduleRegStr);
                std::smatch results2;
                if (std::regex_match(_path, results2, moduleRegex)) {
                    if (results2.size() == 5) {
                        for(auto &v: results2) {
                            cout<< v.str() << endl;
                        }
                    }
                }
            }        
        }
    }
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