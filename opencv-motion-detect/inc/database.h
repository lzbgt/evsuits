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
    typedef int (*cb_verify_str)(const string&);
    typedef int (*cb_verify_json)(const json&);


    int setValue(const string &value, string key, string fileName, cb_verify_str cb);
    int getValue(string &value, string key, string fileName, cb_verify_str cb);
    int setValue(const json &value, string key, string fileName, cb_verify_json cb);
    int getValue(json &value, string key, string fileName, cb_verify_json cb);

    //
    json * findConfigModule(json &config, string sn, string moduleName, int iid);
    //
    int delValue(string key, string fileName);

    // sn, updatetime, boottime
    int setSn(json &info,string fileName=LVDB_FILE_GENERAL);
    int getSn(json &info,string fileName=LVDB_FILE_GENERAL);

    // cloudutils::config
    int getLocalConfig(json &config, string key = "", string fileName=LVDB_FILE_GENERAL);
    int setLocalConfig(json &config, string key = "", string fileName=LVDB_FILE_GENERAL);

    // slices
    int getSlices(json &slices, string fileName);
    int setSlices(json &slices, string fileName);

    // log
    int getLog(json &log, json &writeOptions, string fileName);
    int setLog(json &log, json &readOptions, string fileName);

    typedef int(*cb_traverse_configration_module)(string modname, json &mod);
    int traverseConfigureModules(json &config, cb_traverse_configration_module cb, string moduleName = "");    
}

#endif