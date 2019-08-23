/*
module: database
description: 
author: Bruce.Lu <lzbgt@icloud.com>
update: 2019/08/23
*/

#ifndef __DATABASE_SL__
#define __DATABASE_SL__
extern "C" {
    #include "../vendor/sqlite/sqlite3.h"
}

#include <string>
#include "json.hpp"

#define EV_DB_FILENAME_GENERAL "general.db"
#define EV_DB_FILENAME_LOG "log.db"
#define EV_DB_FILENAME_CONFIG "config.json"
#define EV_DB_FILENAME_SLICES "slices.json"

using namespace std;
using json = nlohmann::json;

namespace DB {
    typedef     int (*callback)(void*,int,char**,char**);
    int exec(void *pUserData, char* fileName, const char* stmt, callback cb);
    int getInfo(void *info, int active, const char*fileName);
    int clearTable(const char *tableName, const char* fileName);
    int setInfo(void* info, const char*fileName);
    int loadLocalConfigration(json &config, string fileName);
    int saveLocalConfigration(json &config, string fileName);
}

#endif