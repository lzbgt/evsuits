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

/// ref: ../config.json
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


} // namespace cloudutils


struct StrException : public std::exception
{
   std::string s;
   StrException(std::string ss) : s(ss) {}
   ~StrException() throw () {} // Updated
   const char* what() const throw() { return s.c_str(); }
};

#endif
