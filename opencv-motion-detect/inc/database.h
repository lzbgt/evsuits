#ifndef __DATABSE_LEVEL_DB_H__
#define __DATABSE_LEVEL_DB_H__
#include "leveldb/db.h"
#include "json.hpp"

using namespace nlohmann;
using namespace std;

namespace LVDB {
    #define LVDB_PATH "/opt/lvldb/"

    // sn, config
    #define LVDB_FILE_GENERAL LVDB_PATH"general.db"

    // slices, log
    #define LVDB_FILE_LOG     LVDB_PATH"log.db"

    #define LVDB_KEY_SUFFIX_BACK "_bak"
    #define LVDB_KEY_SN "SN"
    #define LVDB_KEY_CONFIG "CONFIG"

    //
    int delValue(string key, string fileName);

    // sn, updatetime, boottime
    int setSn(json &info, string fileName);
    int getSn(json &info, string fileName);

    // cloudutils::config
    int getLocalConfig(json &config, string fileName);
    int setLocalConfig(json &config, string fileName);

    // slices
    int getSlices(json &slices, string fileName);
    int setSlices(json &slices, string fileName);

    // log
    int getLog(json &log, json &writeOptions, string fileName);
    int setLog(json &log, json &readOptions, string fileName);    
}

#endif