#ifndef __EV_POST_FILE_H__
#define __EV_POST_FILE_H__

#include <string>
#include <curl/curl.h>
#include <tuple>
#include <vector>
#include "inc/spdlog/spdlog.h"

namespace {

int postFiles(const char*url, vector<tuple<const char* const, const char* const> > params, vector<const char *> fileNames);

}

#endif