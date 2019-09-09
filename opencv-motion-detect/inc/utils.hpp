#ifndef __EV_UTILS_H__
#define __EV_UTILS_H__

#include <vector>
#include <chrono>
#include <thread>
#include <map>
#include <sstream>
#include "json.hpp"
#include "spdlog/spdlog.h"
#include "httplib.h"

using namespace std;
using namespace nlohmann;
using namespace httplib;

// cloudutils
namespace cloudutils
{
/// [deprecated] ref: ../config.json
json registry(json &conf, string sn, string module) {
   json ret;
   string api;
   try{
      api = conf.at("data").at(sn).at("api-cloud").get<string>() + "/register";
      Uri uri=Uri::Parse(api);
      if(uri.Host.empty()||uri.Port.empty()||uri.Protocol.find("http") == string::npos||uri.Path.empty()) {
         string msg = "registry error. invalid api-cloud in config: " + api;
         ret["code"] = 1;
         ret["msg"] = msg;
         spdlog::error(msg);
         return ret;
      }

      Params params;
      params.emplace("sn", sn);
      params.emplace("module", module);
      Client cli(uri.Host.c_str(), stoi(uri.Port));

      auto res = cli.Post("/register", Headers(), params, conf.dump(),  "text/json");
      spdlog::debug("{} {} registry res from cloud : {}", __FILE__, __LINE__, res->body);
      ret = json::parse(res->body);
   }catch(exception &e) {
      ret["code"] = -1;
      string msg = string(__FILE__) + ":" + to_string(__LINE__) + string(": registry exception - ") + e.what();
      ret["msg"] = msg;
      spdlog::error(msg);
   }

   // /Client cli;
   return ret;
}

#define EVCLOUD_REQ_E_CONN -2
#define EVCLOUD_REQ_E_DATA -3
#define EVCLOUD_REQ_E_PARAM -4
#define EVCLOUD_REQ_E_ABORT -5
#define EVCLOUD_REQ_E_NONE 0

/// req config
json reqConfig(json &info){
   json ret;
   string api;
   try{
      api = info.at("api-cloud").get<string>();
      Uri uri=Uri::Parse(api);
      string sn = info.at("sn").get<string>();
      if(uri.Host.empty()||uri.Port.empty()||uri.Protocol.find("http") == string::npos) {
         string msg = string(__FILE__) +":" + to_string(__LINE__) + ": request cloud configuration error. invalid api-cloud in info: " + api;
         ret["code"] = EVCLOUD_REQ_E_PARAM;
         ret["msg"] = msg;
         spdlog::error(msg);
         return ret;
      }

      Params params;
      params.emplace("sn", sn);
      Client cli(uri.Host.c_str(), stoi(uri.Port));

      auto res = cli.Get("/config", Headers(), params);
      if(res == nullptr || res->status != 200) {
         const char *msg = NULL;
         if(res == nullptr) {
            msg = (string("error to connect to server: ") + api + "/config").c_str();
            ret["code"] = EVCLOUD_REQ_E_CONN;
         }else{
            msg = httplib::detail::status_message(res->status);
            ret["code"] = res->status;
         }
         spdlog::debug("failed to reqConfig. {}", msg);         
         ret["msg"] = msg;
      }else{
         spdlog::debug("{} {} registry res from cloud : {}", __FILE__, __LINE__, res->body);
         ret = json::parse(res->body);
      }
   }catch(exception &e) {
      ret["code"] = EVCLOUD_REQ_E_DATA;
      string msg = string(__FILE__) + ":" + to_string(__LINE__) + string(": registry exception - ") + e.what();
      ret["msg"] = msg;
      spdlog::error(msg);
   }

   // /Client cli;
   return ret;
}

} // namespace cloudutils


///
namespace strutils{
vector<string> split(const std::string& s, char delimiter)
{
   std::vector<std::string> tokens;
   std::string token;
   std::istringstream tokenStream(s);
   while (getline(tokenStream, token, delimiter))
   {
      tokens.push_back(token);
   }
   return tokens;
}

}//namespace strutils

namespace cfgutils {
   int getPeerId(string modName, json& modElem, string &peerId, string &peerName) {
      try {
         if(modName == "evmgr") {
            peerId = modElem["sn"].get<string>() + ":evmgr:0";
            peerName = modName;
         }else if(modName == "evml") {
            peerId = modElem["sn"].get<string>() + ":evml" + modElem["type"].get<string>() + ":" + to_string(modElem["iid"]);
            peerName = modName + modElem["type"].get<string>();
         }else{
            peerId = modElem["sn"].get<string>() + ":" + modName + ":" + to_string(modElem["iid"]);
            peerName = modName;
         }
      }catch(exception &e) {
         spdlog::error("failed to get gid for {} in {}: {}", modName, modElem.dump(), e.what());
         return -1;
      }

      return 0;
   }


   json *findModuleConfig(string peerId, json &data) {
      json *ret = NULL;
      auto pp = strutils::split(peerId, ':');
      if(pp.size() != 3) {
         spdlog::error("invalid peerId: {}", peerId);
         return ret;
      }

      string sn = pp[0];
      string modName = pp[1];
      string iid = pp[2];
      //
      string subMn = modName.substr(0,4);
      if(subMn == "evml") {
         subMn = modName.substr(4, modName.size());
      }else{
         subMn = "";
      }

      try{
         for(auto &[k,v]: data.items()) {
            // it's evmgr
            if(modName == "evmgr") {
               if(k == sn) {
                  ret = &v;
                  break;
               }
            }else{
               json &ipcs = v["ipcs"];
               
               for(auto &ipc: ipcs) {
                  json &modules = ipc["modules"];
                  for(auto &[mn, ml]: modules.items()) {
                     for(auto &m: ml) {
                        if(mn == "evml" && !subMn.empty()){
                           if(subMn == m["type"] && m[iid] == iid && m["sn"] == sn && m["enabled"] != 0) {
                              ret = &v;
                              break;
                           }
                        }else if(subMn.empty()){
                           if(mn == modName && m["sn"] == sn && m["iid"] == iid && m["enabled"] != 0) {
                              ret = &v;
                              break;
                           }
                        }
                     }
                     if(ret != NULL) break;
                  }
                  if(ret != NULL) break;
               }
            }
         }
      }catch(exception &e) {
         spdlog::error("find module {} in {} exception: {}", peerId, data.dump(), e.what());
         return NULL;
      }

      return ret;
   }
} // cfgutils


struct StrException : public std::exception
{
   std::string s;
   StrException(std::string ss) : s(ss) {}
   ~StrException() throw () {} // Updated
   const char* what() const throw() { return s.c_str(); }
};

#endif
