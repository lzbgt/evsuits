#include "inc/httplib.h"
#include "inc/zmqhelper.hpp"
#include "inc/json.hpp"
#include "spdlog/spdlog.h"
#include "fmt/format.h"
#include "database.h"
#include <thread>
#include <future>
#include <regex>
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>

std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

using namespace std;
using namespace nlohmann;
using namespace httplib;


class WifiMgr {
    private:
    json info;
    Server srv;
    promise<int> p;
    thread monitor;
    vector<string> ssids;
    string wifiSSID;
    string wifiPasswd;
    int mode, lastMode; // 1: ap; 2: ste
    const string apdCfgPath = "/etc/apd.conf";
    const string wpaCfgPath = "/etc/wpa_supplicant/wpa_supplicant-wlan1.conf";

    json enableMode(int mode){
        json ret;
        ret["code"] = 0;
        ret["msg"] = "ok";

        if( mode == 1) {
            // ap
            // stop all
            spdlog::info("prepare to enter AP mode");
            exec("systemctl stop wap_supplicant@wlan1");
            // exec("systemctl dsiable wap_supplicant@wlan1 ")
            string apdContent = fmt::format("interface=wlan1\ndriver=nl80211\nssid=EVB-{}\nhw_mode=g\n"
            "channel=6\nmacaddr_acl=0\nignore_broadcast_ssid=0\nwpa=0\n", this->info["sn"].get<string>());
            ofstream fileApd(apdCfgPath, ios::out|ios::trunc);
            if(fileApd.is_open()){
                fileApd << apdContent;
                fileApd.close();
                // start hostapd
                exec("hostapd /etc/apd.conf -B");
                // TODO: check result

                /// scan wifis
                string res = exec("iwlist wlan1 scan|grep ESSID");
                ssids.clear();
                httplib::detail::split(&res[0], &res[res.size()], '\n', [&](const char *b, const char *e) {
                    string ssid;
                    ssid.assign(b,e);
                    ssids.push_back(ssid);
                });
                //
            }else{
                ret["code"] = 1;
                string msg = fmt::format("failed to write ap config file to {}", apdCfgPath);
                spdlog::error(msg);
                ret["msg"] = msg;
                return ret;
            }

        }else if(mode == 2) {
            // station mode
            spdlog::info("prepare to enter Station mode");

            // stop hostapd
            exec("pkill hostapd");
            string wpaContent = fmt::format("ctrl_interface=/run/wpa_supplicant\nupdate_config=1\nap_scan=1\n"
            "network={{\nssid=\"{}\"\npsk=\"{}\"\n}}\n", this->wifiSSID, this->wifiPasswd);

            ofstream wpaFile(wpaCfgPath, ios::out|ios::trunc);
            if(wpaFile.is_open()){
                wpaFile << wpaContent;
                wpaFile.close();
                // TODO: verify
                spdlog::info(exec("systemctl enable wap_supplicant@wlan1"));
                spdlog::info(exec("systemctl restart wap_supplicant@wlan1"));
                spdlog::info(exec("dhclient wlan1"));
            }else{
                string msg = fmt::format("failed write wpa config to {}", wpaCfgPath);
                ret["code"] = 2;
                ret["msg"] = msg;
                spdlog::error(msg);
                return ret;
            }

        }

        return ret;
    }

    public:
    WifiMgr(){
        LVDB::getSn(this->info);

        monitor = thread([this](){
            // check /etc/systemd/wpa_supplicant@wlan1.service
            // get wlan1 status
            // get wlan1 ip
            // ping outside address

            // default is AP mode
            this->lastMode = 0;
            while(1){
                // check modes
                this->mode = 1;
                if(this->lastMode != this->mode) {
                    enableMode(this->mode);
                }
                this->lastMode = this->mode;
                this_thread::sleep_for(chrono::seconds(10));
            }
        });

        monitor.detach();

        srv.Get("/wifi", [this](const Request& req, Response& res) {
            string mode = req.get_param_value("mode");
            json ret;
            ret["code"] = 0;
            ret["msg"] = "ok";
            ret["sn"] = this->info["sn"];
            if(!mode.empty()){
                try{
                    auto i = stoi(mode);
                    ret = this->enableMode(i);
                }catch(exception &e){
                    string msg = fmt::format("exception in convert mode {} to int:{}", mode, e.what());
                    ret["code"] = -1;
                    ret["msg"] = msg;
                    spdlog::error(msg);
                }
            }
            
            res.set_content(ret.dump(), "text/json");
        });

        srv.listen("0.0.0.0", 80);
    }
};

int main(){
    WifiMgr mgr;
}