#ifndef __DATABSE_LEVEL_DB_H__
#define __DATABSE_LEVEL_DB_H__
#include "leveldb/db.h"
// #include "rocksdb/db.h"
// #include "rocksdb/slice.h"
// #include "rocksdb/options.h"
#include "json.hpp"

using namespace nlohmann;
using namespace std;

namespace LVDB {
    #define LVDB_PATH "/opt/lvldb/"

    // sn, config
    #define LVDB_FILE_GENERAL LVDB_PATH "general.db"

    // slices, log
    #define LVDB_FILE_LOG     LVDB_PATH"log.db"

    #define LVDB_KEY_SUFFIX_BACK "_bak"
    #define LVDB_KEY_SN "SN"
    #define LVDB_KEY_CONFIG "CONFIG"

    //
    json * findConfigModule(json &config, string sn, string moduleName, int iid);
    //
    int delValue(string key, string fileName);

    // sn, updatetime, boottime
    int setSn(json &info,string fileName=LVDB_FILE_GENERAL);
    int getSn(json &info,string fileName=LVDB_FILE_GENERAL);

    // cloudutils::config
    int getLocalConfig(json &config, string fileName=LVDB_FILE_GENERAL);
    int setLocalConfig(json &config, string fileName=LVDB_FILE_GENERAL);

    // slices
    int getSlices(json &slices, string fileName);
    int setSlices(json &slices, string fileName);

    // log
    int getLog(json &log, json &writeOptions, string fileName);
    int setLog(json &log, json &readOptions, string fileName);    
}

#endif