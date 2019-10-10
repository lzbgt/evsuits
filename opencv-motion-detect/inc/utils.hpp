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
#include <set>
#include <regex>
#include <iterator>
#include <vector>
#include <string>
#include <fstream>
#include <fmt/format.h>

#define EVCLOUD_REQ_E_CONN -2
#define EVCLOUD_REQ_E_DATA -3
#define EVCLOUD_REQ_E_PARAM -4
#define EVCLOUD_REQ_E_ABORT -5
#define EVCLOUD_REQ_E_NONE 0

using namespace std;
using namespace nlohmann;
using namespace httplib;

// cloudutils
namespace cloudutils
{
/// [deprecated] ref: ../config.json
json registry(json &conf, string sn, string module);
/// req config
json reqConfig(json &info);
} // namespace cloudutils


///
namespace strutils{
vector<string> split(const std::string& s, char delimiter);
}//namespace strutils

namespace cfgutils {
   int getPeerId(string modName, json& modElem, string &peerId, string &peerName);
   json *findModuleConfig(string peerId, json &data);
   json getModulesOperFromConfDiff(json& oldConfig, json &newConfig, json &diff, string sn);
   json getModuleGidsFromCfg(string sn, json &data, string caller = "", int ipcIdx = -1);
}

struct StrException : public std::exception
{
   std::string s;
   StrException(std::string ss) : s(ss) {}
   ~StrException() throw () {} // Updated
   const char* what() const throw() { return s.c_str(); }
};

#endif
