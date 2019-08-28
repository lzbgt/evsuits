#include "inc/database.h"
//#include "inc/json.hpp"
#include "spdlog/spdlog.h"
#include <cstdlib>
#include <mutex>
#include <map>

using namespace rocksdb;

string _config_default_tmpl = "{\"time\":0,\"code\":0,\"data\":{\"<SN_MGR>\":{\"sn\":\"<SN_MGR>\",\"addr\":\"127.0.0.1\",\"addr-cloud\":\"<cloud_addr>\",\"proto\":\"zmq\",\"port-cloud\":5556,\"port-router\":5550,\"status\":1,\"ipcs\":[{\"addr\":\"172.31.0.51\",\"proto\":\"rtsp\",\"user\":\"admin\",\"password\":\"FWBWTU\",\"status\":0,\"modules\":{\"evpuller\":[{\"sn\":\"<SN_PULLER>\",\"addr\":\"127.0.0.1\",\"iid\":1,\"port-pub\":5556,\"status\":0}],\"evpusher\":[{\"sn\":\"<SN_PUSHER>\",\"iid\":1,\"urlDest\":\"rtsp://40.73.41.176:554/test1\",\"user\":\"\",\"password\":\"\",\"token\":\"\",\"enabled\":1,\"status\":0}],\"evslicer\":[{\"sn\":\"<SN_SLICER>\",\"iid\":1,\"path\":\"slices\",\"enabled\":1,\"status\":0}],\"evml\":[{\"type\":\"motion\",\"sn\":\"<SN_ML>\",\"iid\":1,\"enabled\":1,\"status\":0}]}}]}}}";

const string _sn_tmpl[] = {"<SN_MGR>", "<SN_PULLER>", "<SN_PUSHER>", "<SN_SLICER>", "<SN_ML>"};
const string _addr_tmpl[] = {"<ADDR_CAMERA>"};


// TODO:
string getStrRand(int length)
{
    static bool bRand = false;
    if(!bRand) {
        srand(time(NULL));
        bRand = true;
    }
    static string charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";
    string result;
    result.resize(length);

    srand(time(NULL));
    for (int i = 0; i < length; i++)
        result[i] = charset[rand() % charset.length()];

    return result;
}


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

    typedef int (*cb_verify_str)(const string&);
    typedef int (*cb_verify_json)(const json&);
    
    int getValue(string &value, string key, string fileName, cb_verify_str cb) {
        int ret = 0;
        DB* pdb = _getDB(fileName);
        Status s = pdb->Get(ReadOptions(), key, &value);
        if(!s.ok()) {
            spdlog::debug("failed to get {} from {}: {}",key, fileName, s.ToString());
            return -1;
        }
        if(cb != NULL) {
            ret = cb(value);
        }

        return ret;
    }

    int setValue(const string &value, string key, string fileName, cb_verify_str cb) {
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
            spdlog::debug("get old {} error {}:{}", key, fileName, s.ToString());
        }

        s = pdb->Put(WriteOptions(), key, value);
        if(!s.ok()) {
            spdlog::error("failed to put {} -> {}: {}", key, value, s.ToString());
            return -2;
        }

        if(!oldVal.empty()) {
            s = pdb->Put(WriteOptions(), key + LVDB_KEY_SUFFIX_BACK, oldVal);
            if(!s.ok()) {
                spdlog::error("failed to put backup {} -> {}: {}", key, oldVal, s.ToString());
                return -2;
            }
        }

        return 0;
    }

    int getValue(json &value, string key, string fileName, cb_verify_json cb) {
        string s;
        int ret = getValue(s, key, fileName, NULL);
        if(ret < 0) {
            return ret;
        }
        json j;
        try{
            j = json::parse(s);
            if(cb != NULL) {
                ret =  cb(j);
                if(ret < 0) {
                    return ret;
                }
            }
            
            value = j;
        }catch(exception &e) {
            spdlog::error("failed to parse {} -> {} {}: {}", key, s, fileName, e.what());
            return -2;
        }
        return 0;
    }

    int setValue(json &value, string key, string fileName, cb_verify_json cb) {
        int ret = 0;
        if(cb != NULL) {
            ret = cb(value);
            if(ret < 0) {
                return ret;
            }
        }

        ret = setValue(value.dump(), key,fileName, NULL);

        return ret;
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
    int _validateSn(const json &info) {
        if(info.count("sn") == 0||info.count("updatetime") == 0||info.count("lastboot") == 0) {
                spdlog::error("invalid sn config:{}", info.dump());
                return -1;
        }

        return 0;
    }

    int getSn(json &info, string fileName){
        int ret = 0;
        ret = getValue(info, LVDB_KEY_SN, fileName, _validateSn);

        if(ret < 0) {
            // create default sn.
            string sn = getStrRand(8);
            info["sn"] = sn;
            spdlog::warn("no local sn set. create a new one: {}", sn);
            auto tsNow = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
            info["lastboot"] = tsNow;
            info["updatetime"] = tsNow;
            ret = setSn(info, fileName);
            if(ret < 0) {
                spdlog::error("failed to save new generated sn");
                exit(1);
            }else{
                // replace sn
                int idx = 0;
                for(auto &j:_sn_tmpl) {
                    idx = 0;
                    while(true) {
                        idx = _config_default_tmpl.find(j, idx);
                        if(idx == string::npos) break;
                        _config_default_tmpl.replace(idx, j.size(), sn);
                        idx+=sn.size();
                    }
                }

                // replace camera addr, user, password, cloud-addr
                spdlog::debug("new config: {}", _config_default_tmpl);
                return setValue(_config_default_tmpl, LVDB_KEY_CONFIG, fileName, NULL);
            }
        }

        return ret;
    };

    int setSn(json &info, string fileName){
        return setValue(info, LVDB_KEY_SN, fileName, _validateSn);
    };

    // config
    int _validateConfig(const json &config) {
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