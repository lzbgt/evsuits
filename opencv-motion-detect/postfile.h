#ifndef __EV_POST_FILE_H__
#define __EV_POST_FILE_H__

#include <string>
#include <curl/curl.h>
#include "inc/spdlog/spdlog.h"
#include "inc/json.hpp"

namespace netutils{
using namespace std;
using namespace nlohmann;
int postFiles(string &url, json &params, json &fileNames, string &resp);

}

#endif