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
         ret["code"] = 1;
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
            ret["code"] = -2;
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
      ret["code"] = -1;
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
            peerId = modElem["sn"].get<string>() + ":evml" + modElem["type"].get<string>() + ":" + modElem["iid"].get<string>();
            peerName = modName + modElem["type"].get<string>();
         }else{
            peerId = modElem["sn"].get<string>() + ":" + modName + ":" + modElem["iid"].get<string>();
            peerName = modName;
         }
      }catch(exception &e) {
         spdlog::error("failed to get gid for {} in {}: {}", modName, modElem.dump(), e.what());
         return -1;
      }

      return 0;
   }
}


struct StrException : public std::exception
{
   std::string s;
   StrException(std::string ss) : s(ss) {}
   ~StrException() throw () {} // Updated
   const char* what() const throw() { return s.c_str(); }
};

#endif
