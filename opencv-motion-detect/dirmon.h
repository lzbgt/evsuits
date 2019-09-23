#ifndef __EV_DIR_MON_H__
#define __EV_DIR_MON_H__

#include <vector>
#include <map>
#include <set>
#include <algorithm>

#include "libfswatch/c++/path_utils.hpp"
#include "libfswatch/c++/event.hpp"
#include "libfswatch/c++/monitor.hpp"
#include "libfswatch/c++/monitor_factory.hpp"
#include "libfswatch/c++/libfswatch_exception.hpp"
#include "inc/spdlog/spdlog.h"
using namespace std;
using namespace fsw;

int CreateDirMon(monitor **m, string path, string ext, vector<string> &&events, FSW_EVENT_CALLBACK cb, void *pUserData);
int CloseDirMon(monitor*m);

#endif