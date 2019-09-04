#pragma GCC diagnostic ignored "-Wunused-variable"

#include "inc/database.h"
//#include "inc/json.hpp"
#include "spdlog/spdlog.h"
#include <cstdlib>
#include <mutex>
#include <map>
#include <fstream>
#include <iomanip>
#include <thread>
#include <chrono>

using namespace leveldb;

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
    #define LVDB_ERROR_HELD -1
    #define LVDB_ERROR_OTHER -2

    json * findConfigModule(json &config, string sn, string moduleName, int iid) {
        json *ret = NULL;
        json &data = config["data"];
        for(auto &[k,v]: data.items()){
            json &mgr = data[k];
            if(mgr.count("ipcs") == 0) {
                break;
            }else{
                json &ipcs = mgr["ipcs"];
                for(auto &ipc:ipcs) {
                    if(ipc.count("modules") == 0) {
                        break;
                    }else{
                        json &modules = ipc["modules"];
                        string modname = moduleName.substr(0,4);
                        string sub;
                        if(modname == "evml") {
                            sub = moduleName.substr(4, moduleName.size());
                        }else{
                            modname = moduleName;
                        }

                        if(modules.count(modname) == 0){
                            break;
                        }else{
                            json &module = modules[modname];
                            for(auto &inst: module) {
                                if(inst.count("sn") != 0 && inst["sn"] == sn && inst.count("iid") != 0 && inst["iid"] == iid) {
                                    if(!sub.empty()) {
                                        if(inst.count("type") != 0 && inst["type"] == sub) {
                                            return &inst;
                                        }
                                        // continue
                                    }else{
                                        return &inst;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
        }
        return ret;
    }

    int traverseConfigureModules(json &config, cb_traverse_configration_module cb, string moduleName){
        int ret = 0;
        if(config.count("data") == 0) {
            return -1;
        }
        if(cb == NULL) {
            return -2;
        }

        json &data = config["data"];
        for(auto &[k,v]: data.items()){
            json &mgr = data[k];
            if(mgr.count("ipcs") == 0) {
                break;
            }else{
                json &ipcs = mgr["ipcs"];
                for(auto &ipc:ipcs) {
                    if(ipc.count("modules") == 0) {
                        continue;
                    }else{
                        string modname, sub;
                        if(!moduleName.empty()){
                            modname = moduleName.substr(0,4);
                            if(modname == "evml") {
                                sub = moduleName.substr(4, moduleName.size());
                            }else{
                                modname = moduleName;
                            }
                        }

                        json &modules = ipc["modules"];
                        if(!modname.empty()) {
                            if(modules.count(modname) == 0) {
                                return -3;
                            }else{
                                json &module = modules[modname];
                                if(!sub.empty()) {
                                    for(auto &m:module) {
                                        if(m.count("type") != 0 && m["type"] == sub) {
                                            ret = cb(modname, m);
                                            if(ret <0) {
                                                spdlog::error("failed to traverse and callback config on module: {}", m.dump());
                                                return ret;
                                            }
                                        }
                                    }
                                }
                            }
                        }else{
                            for(auto &[mn, mod]:modules.items()) {
                                for(auto &m:mod) {
                                    ret = cb(mn,m);
                                    if(ret <0) {
                                        spdlog::error("failed to traverse and callback config on module: {}", m.dump());
                                        return ret;
                                    }
                                }
                            }     
                        }                        
                    }
                }
            }    
        }
        return 0;
    }

    int _getDB(string fileName, DB** pdb) {
        static bool bmk = false;
        int ret = 0;
        Options options;
        options.create_if_missing = true;
        if(!bmk) {
            string mkdir_ = "mkdir -p " + fileName;
            //spdlog::info("creating directory: {}", mkdir_);
            ret = system(mkdir_.c_str());
            if(-1 == ret) {
                spdlog::error("failed to create directory for {}", fileName);
                exit(1);
            }
            bmk = true;
        }
        
        int cnt = 0;

        while(cnt < 10000){
            Status s = DB::Open(options, fileName, pdb);
            if(!s.ok()) {
                size_t pos = s.ToString().find("already held by process");
                if(pos != string::npos) {
                    spdlog::warn("failed to open db {}: {}", fileName, "already opened by other");
                    //wait for 100 * 10ms
                    this_thread::sleep_for(chrono::milliseconds(10));
                }
                else{
                    spdlog::error("failed to open db {}: {}", fileName, s.ToString());
                    return LVDB_ERROR_OTHER;
                }
            }else{
                break;
            }
            
            cnt++;
        }
        
        assert(pdb != NULL);

        return 0;
    }

    int clearDB(string fileName) {
        return 0;
    }
    
    int getValue(string &value, string key, string fileName, cb_verify_str cb) {
        int ret = 0;
        DB* pdb = NULL;

        if(fileName.empty()){
            fileName = LVDB_FILE_GENERAL;
        }


        ret = _getDB(fileName, &pdb);
        if(ret < 0) {
            return ret;
        }

        Status s = pdb->Get(ReadOptions(), key, &value);
        if(!s.ok()) {
            spdlog::debug("failed to get {} from {}: {}",key, fileName, s.ToString());
            ret = -1;
        }else{
            if(cb != NULL) {
                ret = cb(value);
            }
        }
        
        delete pdb;
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

        if(fileName.empty()){
            fileName = LVDB_FILE_GENERAL;
        }

        DB* pdb = NULL;
        ret = _getDB(fileName, &pdb);
        if(ret < 0) {
            return ret;
        }
        string oldVal;
        Status s = pdb->Get(ReadOptions(), key, &oldVal);
        if(!s.ok()) {
            spdlog::debug("get old {} error {}:{}", key, fileName, s.ToString());
        }

        s = pdb->Put(WriteOptions(), key, value);
        if(!s.ok()) {
            spdlog::error("failed to put {} -> {}: {}", key, value, s.ToString());
            ret = -1;
            goto togo_end;
        }

        if(!oldVal.empty()) {
            s = pdb->Put(WriteOptions(), key + LVDB_KEY_SUFFIX_BACK, oldVal);
            if(!s.ok()) {
                spdlog::error("failed to put backup {} -> {}: {}", key, oldVal, s.ToString());
                ret = -2;
                goto togo_end;
            }
        }

togo_end:
        delete pdb;

        return ret;
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

    int setValue(const json &value, string key, string fileName, cb_verify_json cb) {
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
        int ret = 0;
        DB* pdb = NULL;
        ret = _getDB(fileName, &pdb);
        if(ret < 0) {
            return ret;
        }
        Status s = pdb->Delete(WriteOptions(), key);
        if(!s.ok()) {
            spdlog::error("failed to delete key {}: {} in {}",s.ToString(), key, fileName);
            ret = -1;
        }

        //delete pdb;
        return ret;
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
                size_t idx = 0;
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

    // int setSn(json &info) {
    //     std::ifstream i("file.json");
    //     json j;
    //     i >> j;
    //     // write prettified JSON to another file
    //     std::ofstream o("pretty.json");
    //     o << std::setw(4) << j << std::endl;
    //     return -1;
    // }
    // int getSn(json &info) {
    //     fstream file;
    //     file.open(LVDB_KEY_SN, ios::out|ios);
    //     if(snfile.e)
    //     json j;
    //     i >> j;
    //     // write prettified JSON to another file
    //     std::ofstream o("pretty.json");
    //     o << std::setw(4) << j << std::endl;
    // }

    // config
    int _validateConfig(const json &config) {
        if(config.count("data") == 0|| config["data"].size() == 0) {
            spdlog::error("invliad config: {}", config.dump());
            return -1;
        }
        return 0;
    }

    int getLocalConfig(json &config, string key, string fileName){
        if(key.empty()) {
            return getValue(config, LVDB_KEY_CONFIG, fileName, _validateConfig); 
        }else{
            return getValue(config, key, fileName, _validateConfig);
        }
          
    };

    int setLocalConfig(json &config, string key, string fileName){
        if(key.empty()) {
            return setValue(config, LVDB_KEY_CONFIG, fileName, _validateConfig);
        }else{
            return setValue(config, key, fileName, _validateConfig);
        } 
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