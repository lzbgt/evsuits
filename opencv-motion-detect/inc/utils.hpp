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
      api = conf.at(sn).at("api-cloud").get<string>() + "/register";

   }catch(exception &e) {

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
