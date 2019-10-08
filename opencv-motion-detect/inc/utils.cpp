#include "utils.hpp"


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
                  // not evml
                  if(subMn.empty()) {
                     if(modules.count(modName) != 0) {
                        json &ml = modules[modName];
                        for(auto &m : ml) {
                           if(m["sn"] == sn && to_string(m["iid"]) == iid && m["enabled"] != 0) {
                              ret = &v;
                              break;
                           }
                        }
                     }  
                  }else{
                     if(modules.count("evml") != 0) {
                        json &ml = modules["evml"];
                        for(auto &m: ml) {
                           if(subMn == m["type"] && to_string(m[iid]) == iid && m["sn"] == sn && m["enabled"] != 0) {
                              ret = &v;
                              break;
                           }
                        }
                     }
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

   vector<string> getModuleGidsOfIpc(json &config, string sn, int ipcId) {
      json tmp;
      if(config.count(sn) == 0) {
         //
         spdlog::error("getModuleGidsOfIpc invalid config for sn {}: {}", sn, config.dump());
      }else{
         auto conf = config[sn];
         if(conf.count("ipcs") == 0 || conf["ipcs"].size() < (ipcId + 1) || conf["ipcs"][ipcId].count("modules") == 0||conf["ipcs"][ipcId]["modules"].size() == 0) {
            spdlog::error("getModuleGidsOfIpc invalid config having no such ipc {} or modules. {}: {}", ipcId, sn, config.dump());
         }else{
            auto modules = conf["ipcs"][ipcId]["modules"];
            for(auto &[mn, mv]: modules.items()) {
               for(auto &m:mv) {
                  if(m.count("sn") == 0 || m.count("iid") == 0||( mn == "evml" && m.count("type") == 0)) {
                     spdlog::error("getModuleGidsOfIpc invalid module config {} in {}", mv.dump(), config.dump());
                  }else{
                     if(m["sn"] == sn) {
                        string gid;
                        if(mn == "evml"){
                           gid = sn + ":evml"+m["type"].get<string>()+":"+ to_string(m["iid"].get<int>());
                        }else{
                           gid = sn + ":" + mn + ":"+ to_string(m["iid"].get<int>());
                        }
                        tmp[gid] = 1;
                     }
                  }
               }
            }
         }
      }

      vector<string> ret;
      for(auto &[k,v]: tmp.items()) {
         ret.push_back(k);
      }

      return ret;
   }
} // cfgutils
