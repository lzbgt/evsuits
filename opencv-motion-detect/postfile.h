#ifndef __EV_POST_FILE_H__
#define __EV_POST_FILE_H__

#include <string>
#include <curl/curl.h>
#include <tuple>
#include <vector>
#include "inc/spdlog/spdlog.h"

namespace netutils{
using namespace std;
int postFiles(string &&url, vector<tuple<string, string> > &&params, vector<string> &&fileNames, string &resp);

}

#endif