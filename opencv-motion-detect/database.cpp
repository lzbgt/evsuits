#include "inc/database.h"
#include <stdio.h>
#include <stdlib.h>
#include <mutex>
#include <map>
#include <vector>
#include <spdlog/spdlog.h>
#include <json.hpp>

using namespace std;
using json = nlohmann::json;

namespace DB {
    map<string, sqlite3 *> maphdb;
    map<string, mutex> mapMut;
    //typedef     int (*callback)(void*,int,char**,char**);
    int exec(void *pUserData, const char* fileName, const char* stmt, callback cb){
        int ret = 0;
        if(fileName == NULL||strlen(fileName) == 0) {
            fileName = const_cast<char*>("default.db");
        }

        sqlite3 *ppdb = NULL;
        try{
            ppdb = maphdb.at(string(fileName));
        }catch(...){
            ppdb = NULL;
        }

        mutex &mut = mapMut[string(fileName)];
        if(ppdb == NULL) {
            std::lock_guard<std::mutex> lock(mut);
            if(ppdb == NULL) {
                ret = sqlite3_open(fileName,&ppdb);
                if(ret != SQLITE_OK)
                {
                    spdlog::error("sqlite3_open: {}",sqlite3_errmsg(ppdb));
                    exit(1);
                }
            }    

            maphdb[string(fileName)] = ppdb; 
        }
        //
        // sprintf(sql,"create table if not exists address (name text, tel text);");
        if(stmt != NULL) {
            std::lock_guard<std::mutex> lock(mut);
            ret = sqlite3_exec(ppdb, stmt, cb, pUserData, NULL);
            if(ret != SQLITE_OK)
            {
                spdlog::error("sqlite3_exec: {}",sqlite3_errmsg(ppdb));
            }
        }
        
        //sqlite3_close(ppdb);
        return ret;
    }

    int _getSlices(void *pUser, int cc, char **cv, char **cn) {
        auto v = static_cast< vector<int>* >(pUser);
        if(cc != v->size()) {
            return SQLITE_ERROR;
        }else{
            for(int i = 0; i < v->size(); i ++) {
                v->at(i) = atoi(cv[0]);
            }
        }
    }

/*
{
   "code":0,
   "time":0,
   "data":{
      "ipc":"172.31.0.51",
      "username":"admin",
      "password":"FWBWTU",
      "services":{
         "evmgr":{
            "sn":"ILS-1",
            "addr":"0.0.0.0",
            "port-pub":5556,
            "port-rep":5557,
            "iid":1
         },
         "evpuller":{
            "sn":"ILS-2",
            "addr":"0.0.0.0",
            "port-pub":5556,
            "port-rep":5557,
            "iid":2
         },
         "evpusher":[
            {
               "sn":"ILS-2",
               "addr":"localhost",
               "iid":2,
               "enabled":1,
               "urlDest":"rtsp://40.73.41.176:554/test1"
            }
         ],
         "evslicer":[
            {
               "sn":"ILS-3",
               "addr":"192.168.0.25",
               "iid":3,
               "path": "/var/lib/slices/"
            }
         ],
         "evml":[
            {
               "feature":"motion",
               "sn":"ILS-4",
               "addr":"192.168.0.26",
               "iid":4
            }
         ]
      }
   }
}
*/

// tables: device, evmgr, evpuller, evpusher, evslice, evml, ipc, log
// schemas:
// info: id, cls text, value text, version, update datetime
//   ex: 1, sn,  ILS112233, NULL, NULL
//       2, evmgr, NULL, 1.2, 2019-09-02
//       3, evpuller, NULL, 1.2, 2019-09-02

// ipc: id, user, passwd, addr, status
//   ex: 1, admin, FWBWTU, 172.31.0.51, 0
// evmgr:
// evpuller: iid, cid, addr, pub, rep, status;
// evpusher: iid, cid, addr, urldest, enabled, status;
// evslicer: iid, cid, addr, urldest, days, miniutes, status;
// evml:     iid, cid, type, addr, enabled, status
//
//
    int setLocalConfig(json config, const char* fileName) {
        int ret; 
        string stmt;
        // init tables;
        stmt = "create table if not exists info(cls text, value text, version text, update datetime, primary key (cls, value));";
        ret = exec(NULL, fileName, stmt.c_str(), NULL);
        if(ret != SQLITE_OK) {
            return ret;
        }
    }

    int getLocalConfig(json config) {

    }

    int getSlices(void *pUser, int iid, const char *fileName) {
        int ret = 0;
        auto v = static_cast< vector<int>* >(pUser);
        string stmt = "select ts from slices where iid="+to_string(iid)+" order by id;";
        ret = exec(pUser, fileName, stmt.c_str(), _getSlices);
        if(ret != SQLITE_OK) {
            // create
            stmt = "create table if not exists slices(id integer, iid integer, ts integer);";
            ret = exec(NULL, fileName, stmt.c_str(), NULL);
            if(ret != SQLITE_OK) {
                spdlog::error("failed create table slices for evslicer {}", iid);
                return ret;
            }else{
                for(int i = 1; i <= v->size(); i ++) {
                    stmt = "insert into slices(id, iid, ts) values(" + to_string(i) + to_string(iid) + ", 0";
                    ret = exec(NULL, fileName, stmt.c_str(), NULL);
                    if(ret != SQLITE_OK) return ret;
                    v->push_back(0);
                }

                stmt = "update slices set ts=1 where id = 1 and iid="+to_string(iid) +";";
                ret = exec(NULL, stmt.c_str(), NULL, NULL);
                if(ret != SQLITE_OK) return ret;
                v->at(0) = 2;
            }
        }else{

        }
        
        return SQLITE_OK;
    }
}