#include "inc/database.h"
//#include "inc/json.hpp"
#include "spdlog/spdlog.h"
#include <cstdlib>
#include <mutex>
#include <map>

using namespace leveldb;
namespace LVDB {
    map<string, DB*> mappDB;
    DB *_getDB(string fileName) {
        DB *pdb = NULL;
        int ret = 0;
        if(mappDB.count(fileName) == 0) {
            //
            string mk = string("mkdir -p ") + LVDB_PATH;
            ret = system(mk.c_str());
            if(ret == -1) {
                spdlog::error("failed to create db path: {}", LVDB_PATH);
            }else{
                Options options;
                options.create_if_missing = true;
                Status s = DB::Open(options, fileName, &pdb);
                if(!s.ok()) {
                    spdlog::error("failed to open db {}: {}", fileName, s.ToString());
                }
                mappDB[fileName] = pdb;
            }   
        }else{
            pdb = mappDB[fileName];
        }

        assert(pdb != NULL);

        return pdb;
    }

    int clearDB(string fileName) {
        return 0;
    }

    typedef int (*cb_verify)(json &);

    int getValue(json &value, string key, string fileName, cb_verify cb) {
        DB* pdb = _getDB(fileName);
        string oldVal;
        int ret = 0;
        json j;
        Status s = pdb->Get(leveldb::ReadOptions(), key, &oldVal);
        if(!s.ok()) {
            spdlog::error("failed to get {} from {}: {}",key, fileName, s.ToString());
            return -1;
        }
        try{
            j = json::parse(oldVal);
            if(cb != NULL) {
                ret =  cb(j);
                if(ret < 0) {
                    return ret;
                }
            }
            
            value = j;
        }catch(exception &e) {
            spdlog::error("failed to parse {} -> {} {}: {}", key, oldVal, fileName, e.what());
            return -2;
        }
        return 0;
    }

    int setValue(json &value, string key, string fileName, cb_verify cb) {
        int ret = 0;
        if(cb != NULL) {
            ret = cb(value);
            if(ret < 0) {
                return ret;
            }
        }

        DB* pdb = _getDB(fileName);
        string oldVal;
        Status s = pdb->Get(ReadOptions(), key, &oldVal);
        if(!s.ok()) {
            spdlog::warn("get old {} error {}:{}", key, fileName, s.ToString());
        }

        s = pdb->Put(leveldb::WriteOptions(), key, value.dump());
        if(!s.ok()) {
            spdlog::error("failed to put {} -> {}: {}", key, value.dump(), s.ToString());
            return -2;
        }
        if(!oldVal.empty()) {
            s = pdb->Put(leveldb::WriteOptions(), key+LVDB_KEY_SUFFIX_BACK, oldVal);
            if(!s.ok()) {
                spdlog::error("failed to put backup {} -> {}: {}", key, oldVal, s.ToString());
                return -2;
            }
        }

        return 0;
    }

    int delValue(string key, string fileName) {
        DB* pdb = _getDB(fileName);
        Status s = pdb->Delete(WriteOptions(), key);
        if(!s.ok()) {
            spdlog::error("failed to delete key {}: {} in {}",s.ToString(), key, fileName);
            return -1;
        }
        return 0;
    }
    // sn
    // {"sn":string, "updatetime": string, "lastboot": string}
    int _validateSn(json &info) {
        if(info.count("sn") == 0||info.count("updatetime") == 0||info.count("lastboot") == 0) {
                spdlog::error("invalid sn config:{}", info.dump());
                return -1;
        }

        return 0;
    }

    int getSn(json &info, string fileName){
        return getValue(info, LVDB_KEY_SN, fileName, _validateSn);
    };

    int setSn(json &info, string fileName){
        return setValue(info, LVDB_KEY_SN, fileName, _validateSn);
    };

    // config
    int _validateConfig(json &config) {
        if(config.count("data") == 0|| config["data"].size() == 0) {
            spdlog::error("invliad config: {}", config.dump());
            return -1;
        }
        return 0;
    }

    int getLocalConfig(json &config, string fileName){
        return getValue(config, LVDB_KEY_CONFIG, fileName, _validateConfig);
    };

    int setLocalConfig(json &config, string fileName){
        return setValue(config, LVDB_KEY_CONFIG, fileName, _validateConfig);
    };

    // slices
    int saveSlices(json &slices, string fileName){
        return 0;
    };
    int loadSlices(json &slices, string fileName){
        return 0;
    };

    // log
    int saveLog(json &log, json &writeOptions, string fileName){
        return 0;
    };
    int readLog(json &log, json &readOptions, string fileName){
        return 0;
    };

}