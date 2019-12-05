#include "inc/httplib.h"
#include "inc/zmqhelper.hpp"
#include "inc/json.hpp"
#include "inc/fs.h"
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


// ilabservice hardware, non-portable code
#define EMBED_HW_ILS
#ifdef EMBED_HW_ILS

#include <signal.h>
#include <fcntl.h>
#include <fcntl.h>

#define DEV_NAME "/dev/int_gpio_1"

typedef struct _key_msg {
    int key_id;
    unsigned long time;
    int count;
} key_msg, *KEY_MSG;


mutex mutLed;
static char lightLed(bool on)
{
    static int fd = 0;
    int size;
    const char *path = "/sys/class/gpio_sw/PL10/light";
    lock_guard<mutex> lg(mutLed);
    if(fd <= 0) {
        fd = open(path, O_RDWR);
        if (fd<0) {
            spdlog::error("failed to open {}", path);
            return -1;
        }
    }

    char buf[] = "0";
    if(on) {
        buf[0] = '1';
    }

    size = write(fd, buf, strlen(buf));

    if (size<0) {
        spdlog::error("failed to write led", path);
        return -1;
    }
    return 0;
}

list<string> ledPattList;

void ledPattDefault(){
    ledPattList.push_back("1");
}

void ledPattAPMode(){
    ledPattList.push_back("10");
}

void ledNoNetwork(){
    ledPattList.push_back("1000");
}

#else
void ledPattDefault(){

}

void ledPattAPMode(){

}

void ledNoNetwork(){

}
#endif

std::string exec(const char* cmd)
{
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        // throw std::runtime_error("popen() failed!");
        return result;
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
    string devSn;
    string baseDir = "web/main/dist";
    Server srv;
    promise<int> p;
    thread monitor;
    json wifiData;
    int mode, lastMode; // 0:no; network 1: ap; 2: ste
    mutex mutMode;
    int mode1Cnt = 0;
    const string apdCfgPath = "/etc/apd.conf";
    const string wpaCfgPath = "/etc/wpa_supplicant/wpa_supplicant-wlan1.conf";

    void scanWifi()
    {
        lock_guard<mutex> lk(mutMode);

        /// get sn
        LVDB::getSn(this->info);
        devSn = this->info["sn"];

        /// get wifi mac & IP
        auto mac = exec("ifconfig wlan1|grep ether|awk '{print $2}'");
        auto ip = exec("ifconfig wlan1|grep -v inet6|grep inet|awk '{print $2}'");
        if(ip.size() > 1) {
            ip = ip.substr(0, ip.size() -1);
        }
        else {
            ip = "";
        }
        if(mac.size() > 1) {
            mac = mac.substr(0, mac.size() -1);
        }
        else {
            mac = "";
        }
        wifiData["wifi"]["ip"] = ip;
        wifiData["wifi"]["mac"] = mac;
        spdlog::info("evwifi {} ip: {}, mac: {}", this->devSn, ip, mac);

        /// get connected wifi ssid
        if(!ip.empty() && ip != "192.168.0.1") {
            auto ssid = exec("grep ssid /etc/wpa_supplicant/wpa_supplicant-wlan1.conf 2> /dev/null|awk '{print substr($1, 6)}'");
            if(ssid.size() >=4) {
                ssid = ssid.substr(1, ssid.size() - 3);
                wifiData["wifi"]["ssid"] = ssid;
                spdlog::info("evwifi {} ssid: {}", this->devSn, ssid);
            }
            else {
                if(wifiData["wifi"].count("ssid") != 0) {
                    wifiData["wifi"].erase("ssid");
                }
            }

            auto password = exec("grep psk /etc/wpa_supplicant/wpa_supplicant-wlan1.conf 2> /dev/null|awk '{print substr($1, 5)}'");
            if(password.size() >=4) {
                password = password.substr(1, password.size() - 3);
                wifiData["wifi"]["password"] = password;
                spdlog::info("evwifi {} password: {}", this->devSn, password);
            }
            else {
                if(wifiData["wifi"].count("password") != 0) {
                    wifiData["wifi"].erase("password");
                }
            }
        }
        else {
            if(wifiData["wifi"].count("ssid") != 0) {
                wifiData["wifi"].erase("ssid");
            }
        }

        // scan wifi
        string res = exec("iwlist wlan1 scan|grep ESSID|awk {'print $1'}");
        wifiData["wifi"]["ssids"].clear();
        httplib::detail::split(&res[0], &res[res.size()], '\n', [&](const char *b, const char *e) {
            string ssid;
            ssid.assign(b,e);
            wifiData["wifi"]["ssids"].push_back(ssid);
        });
    }

    public:
    json enableMode(int mode)
    {
        lock_guard<mutex> lk(mutMode);
        json ret;
        ret["code"] = 0;
        ret["msg"] = "ok";
        this->mode1Cnt = 0;

        if( mode == 1) {
            // ap
            this->mode = 1;
            spdlog::info("evwifi {} prepare to enter AP mode", devSn);
            // exec("systemctl dsiable wpa_supplicant@wlan1 ")
            string apdContent = fmt::format("interface=wlan1\ndriver=nl80211\nssid=EVB-{}\nhw_mode=g\n"
                                            "channel=6\nmacaddr_acl=0\nignore_broadcast_ssid=0\nwpa=0\n", this->info["sn"].get<string>());
            ofstream fileApd(apdCfgPath, ios::out|ios::trunc);
            if(fileApd.is_open()) {
                fileApd << apdContent;
                fileApd.close();
                // start hostapd
                auto t = thread([this]() {
                    system("pkill hostapd;systemctl stop wpa_supplicant@wlan1;ifconfig wlan1 down;"
                           "ifconfig wlan1 up;ifconfig wlan1 192.168.0.1;hostapd /etc/apd.conf -B");
                    // TODO: check result
                });
                t.detach();
                ledPattAPMode();
            }
            else {
                ret["code"] = 1;
                string msg = fmt::format("failed to write ap config file to {}", apdCfgPath);
                spdlog::error("evwifi {} {}", devSn,msg);
                ret["msg"] = msg;
            }
        }
        else if(mode == 2) {
            // station mode
            this->mode = 2;
            spdlog::info("evwifi {} prepare to enter Station mode", devSn);
            if( wifiData["wifi"].count("ssid") == 0 ||  wifiData["wifi"]["ssid"].size() == 0 ||
                    wifiData["wifi"].count("password") == 0 ||  wifiData["wifi"]["password"].size() == 0) {
                string msg = fmt::format("no valid ssid/password provided");
                spdlog::error("evwifi {} {}", devSn, msg);
                ret["msg"] = msg;
                ret["code"] = 3;
            }
            else {
                string wpaContent = fmt::format("ctrl_interface=/run/wpa_supplicant\nupdate_config=1\nap_scan=1\n"
                                                "network={{\nssid=\"{}\"\npsk=\"{}\"\n}}\n", this->wifiData["wifi"]["ssid"].get<string>(), this->wifiData["wifi"]["password"].get<string>());
                ofstream wpaFile(wpaCfgPath, ios::out|ios::trunc);
                if(wpaFile.is_open()) {
                    wpaFile << wpaContent;
                    wpaFile.close();
                    // TODO: verify
                    auto t = thread([this]() {
                        // delay for rest return (ifdown caused no networking available)
                        this_thread::sleep_for(chrono::seconds(1));
                        system("pkill hostapd; pkill dhclient;systemctl enable wpa_supplicant@wlan1;systemctl restart wpa_supplicant@wlan1;"
                               "/sbin/ifdown -a --read-environment; /sbin/ifup -a --read-environment");
                        // check status
                        auto s = exec("ifconfig wlan1|grep -v inet6|grep inet");
                        if(s.empty()) {
                            spdlog::error("evwifi {} failed to connect to wifi {} with password {}. initiazing AP mode", this->devSn,
                                          this->wifiData["wifi"]["ssid"].get<string>(), this->wifiData["wifi"]["password"].get<string>());
                            this->mode = 0;
                            ledNoNetwork();
                        }
                        else {
                            system("systemctl restart evdaemon");
                            spdlog::info("evwifi {} successfully connected to wifi {}", this->devSn, this->wifiData["wifi"]["ssid"].get<string>());
                            ledPattDefault();
                        }
                    });
                    t.detach();
                }
                else {
                    string msg = fmt::format("failed write wpa config to {}", wpaCfgPath);
                    ret["code"] = 2;
                    ret["msg"] = msg;
                    spdlog::error("evwifi {} {}", devSn, msg);
                }
            }
        }

        return ret;
    }

public:

    void run()
    {
        srv.listen("0.0.0.0", 80);
    };

    WifiMgr()
    {
        LVDB::getSn(this->info);
        devSn = this->info["sn"];
        mode = 0;
        wifiData["info"] = this->info;
        wifiData["wifi"] = json();
        wifiData["wifi"]["ssids"] = json();
        //wifiData["wifi"]["ssid"] = string;
        //wifiData["wifi"]["password"] = string;

        monitor = thread([this]() {
            while(1) {
                /// background wifi scanning
                if(this->mode != 2) {
                    this->scanWifi();
                }
                // TODO: flash light
                string ip, ssid, password;

                if(this->wifiData["wifi"].count("ip") != 0) {
                    ip = this->wifiData["wifi"]["ip"];
                }
                if(this->wifiData["wifi"].count("ssid") != 0) {
                    ssid = this->wifiData["wifi"]["ssid"];
                }

                if(this->wifiData["wifi"].count("password") != 0) {
                    password = this->wifiData["wifi"]["password"];
                }

                {
                    // auto s = exec("ifconfig wlan1|grep -v inet6|grep inet|awk '{print $2}'");

                    if(ip.empty()) {
                        // switch to ap automatically
                        if(this->mode == 0) {
                            spdlog::info("evwifi {} detects no wifi connection, enabling AP mode", this->devSn);
                            this->enableMode(1);
                        }
                        else if(this->mode == 1) {
                            // maybe give it a try to switch into mode2 is not a bad idea
                            this->mode1Cnt++;
                            if(!ssid.empty() && !password.empty() && mode1Cnt % 600) {
                                spdlog::info("evwifi {} give it a try to mode2, since configuration exists.", this->devSn);
                                this->enableMode(2);
                            }
                        }
                    }
                    else {
                        // having wifi ip
                        if(ip == "192.168.0.1") {
                            this->mode = 1;
                        }
                        else if(!ssid.empty() && !password.empty()) {
                            this->mode = 2;
                        }
                        else {
                            spdlog::info("evwifi {} invalid state(having wifi IP but no config), switch to AP mode", this->devSn);
                            this->enableMode(1);
                        }
                    }
                }

                this_thread::sleep_for(chrono::seconds(60));
            }
        });

        monitor.detach();

        //Headers headers = {{'Access-Control-Allow-Origin', '*'}, {'Access-Control-Allow-Headers', 'Origin, X-Requested-With, Content-Type, Accept'}};
        srv.set_base_dir(this->baseDir.c_str());

        srv.Get("/wifi", [this](const Request& req, Response& res) {
            this->mode1Cnt = 0;
            string mode = req.get_param_value("mode");
            json ret;
            ret["code"] = 0;
            ret["msg"] = "ok";
            string scan = req.get_param_value("scan");
            if(!scan.empty()) {
                if(scan == "true") {
                    this->scanWifi();
                    ret["wifiData"] = this->wifiData;
                }
                else {
                    ret["wifiData"] = this->wifiData;
                }
            }

            if(scan.empty() && !mode.empty()) {
                try {
                    auto i = stoi(mode);
                    if(i == 2) {
                        lock_guard<mutex> lk(mutMode);
                        string ssid = req.get_param_value("ssid");
                        string password = req.get_param_value("password");
                        if(ssid.empty()||password.empty()) {
                            string msg = fmt::format("no valid ssid/password provided");
                            spdlog::error("evwifi {} {}", this->devSn, msg);
                            ret["msg"] = msg;
                            ret["code"] = 3;
                        }
                        else {
                            this->mode = 2;
                            this->wifiData["wifi"]["ssid"] = ssid;
                            this->wifiData["wifi"]["password"] = password;
                        }
                    }

                    if(ret["code"] == 0) {
                        ret = this->enableMode(i);
                    }

                }
                catch(exception &e) {
                    string msg = fmt::format("exception in convert mode {} to int:{}", mode, e.what());
                    ret["code"] = -1;
                    ret["msg"] = msg;
                    spdlog::error("evwifi {} {}", devSn, msg);
                }
            }

            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept");

            res.set_content(ret.dump(), "text/json");
        });
    }
};


WifiMgr mgr;

#ifdef EMBED_HW_ILS
static int key_event_fd;
key_msg key_event_msg;
void hand_sig(int sig) {
	read(key_event_fd, &key_event_msg, sizeof(key_event_msg));
	spdlog::info("key event id {}, time {}, count {}", key_event_msg.key_id, key_event_msg.time, key_event_msg.count);
    if(key_event_msg.count == 5) {
        // switch AP mode
        mgr.enableMode(1);
    }else if(key_event_msg.count == 5 && key_event_msg.time * 10 >= 10 * 1000){
        // clear SN && restart evdaemon
        system("rm -fr /opt/lvldb && systemctl restart evdaemon");
    }else if((key_event_msg.count == 1 && key_event_msg.time * 10 >= 10 * 1000)){
        // restart evdaemon only
        system("systemctl restart evdaemon");
    }
}
#endif

int main()
{
#ifdef EMBED_HW_ILS
    int flags;
    key_event_fd = open(DEV_NAME, O_RDWR);
    if (key_event_fd<0) {
        printf("open %s error \n", DEV_NAME);
        return -1;
    }

    signal(SIGIO, hand_sig);

    fcntl(key_event_fd, F_SETOWN, getpid());
    flags = fcntl(key_event_fd, F_GETFL);
    fcntl(key_event_fd, F_SETFL, flags|FASYNC);


    // LED

    auto tLed = thread([&]() {
        string lastPatt = "1";
        char lastMode = '0';
        while(1) {
            if(ledPattList.size() != 0) {
                lastPatt =  ledPattList.front();
                ledPattList.pop_front();
            }
            
            for(auto &c: lastPatt) {
                if(c == lastMode){
                    //skip
                }else{
                    if(c == '1'){
                    // on
                        lightLed(true);
                    }else{
                        lightLed(false);
                    }
                    lastMode = c;
                }
                this_thread::sleep_for(chrono::milliseconds(500));
            }
            this_thread::sleep_for(chrono::milliseconds(500));
        }
    });

    tLed.detach();
#endif

    mgr.run();

#ifdef EMBED_HW_ILS
    close(key_event_fd);
#endif
}