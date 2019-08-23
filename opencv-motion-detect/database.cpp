/*
module: database
description:
author: Bruce.Lu <lzbgt@icloud.com>
update: 2019/08/23
*/

#include "inc/database.h"
#include <stdio.h>
#include <stdlib.h>
#include <mutex>
#include <map>
#include <vector>
#include <spdlog/spdlog.h>

using namespace std;

namespace DB {
map<string, sqlite3 *> maphdb;
map<string, mutex> mapMut;
static bool bRand = false;
//typedef     int (*callback)(void*,int,char**,char**);
sqlite3* exec(void *pUserData, const char* fileName, const char* stmt, callback cb)
{
    int ret = 0;
    if(fileName == NULL||strlen(fileName) == 0) {
        fileName = const_cast<char*>("default.db");
    }

    sqlite3 *pdb = NULL;
    try {
        pdb = maphdb.at(string(fileName));
    }
    catch(...) {
        pdb = NULL;
    }

    mutex &mut = mapMut[string(fileName)];
    if(pdb == NULL) {
        std::lock_guard<std::mutex> lock(mut);
        if(pdb == NULL) {
            ret = sqlite3_open(fileName,&pdb);
            if(ret != SQLITE_OK) {
                spdlog::error("sqlite3_open {}: {}",fileName, sqlite3_errmsg(pdb));
                exit(1);
            }
        }

        maphdb[string(fileName)] = pdb;
    }
    //
    // sprintf(sql,"create table if not exists address (name text, tel text);");
    if(stmt != NULL) {
        std::lock_guard<std::mutex> lock(mut);
        ret = sqlite3_exec(pdb, stmt, cb, pUserData, NULL);
        if(ret != SQLITE_OK) {
            spdlog::debug("sqlite3_exec {} to file {}: {}",stmt, fileName, sqlite3_errmsg(pdb));
        }
    }

    //sqlite3_close(pdb);
    return pdb;
}

string genStrRand(int length)
{
    if(!bRand) {
        srand(time(NULL));
        bRand = true;
    }
    static string charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";
    string result;
    result.resize(length);

    srand(time(NULL));
    for (int i = 0; i < length; i++)
        result[i] = charset[rand() % charset.length()];

    return result;
}

int clearTable(const char *tableName, const char* fileName){
    sqlite3 * pdb = NULL;
    string stmt = "delete from " + string(tableName) + ";";
    pdb = exec(NULL, fileName, stmt.c_str(), NULL);
    if(sqlite3_errcode(pdb) != SQLITE_OK) {
        spdlog::error("failed to clear table {} in {}: {}", tableName, fileName, sqlite3_errmsg(pdb));
        return sqlite3_errcode(pdb);
    }
    return 0;
}


// info: sn, active integer, updatetime datetime, lastboot datetime
//   eg: "ILSAGENTSN1", ts

int setInfo(void* info, const char*fileName)
{
    sqlite3 * pdb = NULL;
    auto v = static_cast<json*>(info);
    if(v==NULL||v->count("sn") == 0 ||v->count("lastboot") == 0) {
        spdlog::error("failed to set info to file {}, parameter error: {}", fileName, v->dump());
        return -1;
    }

    char buf[1024] = {0};

    sprintf(buf, "create table if not exists info(sn text, active integer, updatetime datetime, lastboot datetime);");
    pdb = exec(NULL, fileName, buf, NULL);
    if(sqlite3_errcode(pdb) != SQLITE_OK) {
        spdlog::error("failed to create table info to file {}: {}", fileName, sqlite3_errmsg(pdb));
        return sqlite3_errcode(pdb);
    }

    // delete old backup


    sprintf(buf, "delete from info where active=0;update info set active=0;");
    pdb = exec(NULL, fileName, buf, NULL);
    if(sqlite3_errcode(pdb) != SQLITE_OK) {
        spdlog::error("setInfo failed to update info to file {}: {}", fileName, sqlite3_errmsg(pdb));
        return sqlite3_errcode(pdb);
    }

    sprintf(buf, "insert into info(sn, active, updatetime, lastboot) values('%s', 1, DateTime('now'), '%s');",
            v->at("sn").get<string>().c_str(), v->at("lastboot").get<string>().c_str());
    pdb = exec(NULL, fileName, buf, NULL);
    if(sqlite3_errcode(pdb) != SQLITE_OK) {
        spdlog::error("failed to insert into info to file {}: {}", fileName, sqlite3_errmsg(pdb));
        return sqlite3_errcode(pdb);
    }
    return 0;
}


int _getInfo(void *info, int cc, char **cv, char **cn){
    auto v = static_cast<json*>(info);
    json r;
    if(cc == 0) {
        return SQLITE_NOTFOUND;
    }

    for(int i = 0; i < cc; i++){
        if(strncmp(cn[i], "active", strlen("active")) == 0) {
            r.emplace(cn[i], atoi(cv[i]));
        }else{
            r.emplace(cn[i], cv[i]);
        }
    }

    v->emplace_back(r);

    return 0;
}

int getInfo(void *info, int active, const char*fileName) {
    sqlite3 * pdb = NULL;

    auto v = static_cast<json*>(info);
    if(v == NULL||v->size()!= 0) {
        spdlog::error("getInfo in {} param error: userData must be addr ptr to empty json object");
        return -1;
    }
    
    string stmt;
    if(active <0){
        stmt = "select sn, active, updatetime, lastboot from info;";
    }else{
        stmt = "select sn, active, updatetime, lastboot from info where active="+to_string(active) +";";
    }
   
    pdb = exec(info, fileName, stmt.c_str(), _getInfo);
    if(sqlite3_errcode(pdb) != SQLITE_OK) {
        spdlog::error("failed to get info to file {}: {}", fileName, sqlite3_errmsg(pdb));
        return sqlite3_errcode(pdb);
    }

    spdlog::debug("getInfo to file {}: {}", fileName, v->dump());

    return 0;
}

// modules: id integer, pid integer, iid integer, cls text, sn text, config text, version text, online integer, enabled integer, updatetime datetime, lastboot datetime, active integer
// eg: 2, 0, NULL, evmgr, ILSEVMGR1, "xxxx", 1, 1, ts
//   : 3, 2, NULL, ipc, NULL, "xxx", 1, 1, ts
//   : 5, 3, 1, evpuller, "ILSEVPULLER1", "xxx", 1, 1, ts
// cls = evmgr|ipc|evpuller|evpusher|evslicer|ml

int createModulesTable(const char *fileName){
    sqlite3 * pdb = NULL;

    string stmt = "create table if not exists modules(id integer, pid integer, iid integer, cls text, sn text, config text, version text, online integer, enabled integer, updatetime datetime, lastboot datetime, active integer);";
    pdb = exec(NULL, fileName, stmt.c_str(), NULL);
    if(sqlite3_errcode(pdb) != SQLITE_OK) {
        spdlog::error("failed to create table modules to file {}: {}", fileName, sqlite3_errmsg(pdb));
        return sqlite3_errcode(pdb);
    }

    return 0;
}

int setModulesConfig(void *info, const char*fileName) {
    sqlite3 * pdb = NULL;
    auto v = static_cast<json*>(info);
    if(v==NULL||v->size() == 0||v->count("data") == 0 || v->at("data").size() == 0) {
        spdlog::error("failed to setModulesConfig to file {}, parameter error: {}", fileName, v->dump());
        return -1;
    }

    char buf[1024] = {0};

    // delete old backup config and backup current config.
    sprintf(buf, "delete from modules where active=0; update modules set active=0;");
    pdb = exec(NULL, fileName, buf, NULL);
    if(sqlite3_errcode(pdb) != SQLITE_OK) {
        spdlog::error("setModulesConfig failed to update to file {}: {}", fileName, sqlite3_errmsg(pdb));
        return sqlite3_errcode(pdb);
    }

    // construct records from json
    //auto data = v->


    sprintf(buf, "create table if not exists info(sn text, active integer, updatetime datetime, lastboot datetime);");
    pdb = exec(NULL, fileName, buf, NULL);
    if(sqlite3_errcode(pdb) != SQLITE_OK) {
        spdlog::error("failed to create table modules to file {}: {}", fileName, sqlite3_errmsg(pdb));
        return sqlite3_errcode(pdb);
    }
    return 0;
}

int _getModulesConfig(void *info, int cc, char **cv, char **cn){
    return 0;
}
int getModulesConfig(void *info, const char*fileName) {
    sqlite3 * pdb = NULL;

    auto v = static_cast<json*>(info);
    if(v == NULL||v->size() != 0) {
        spdlog::error("getModule in {} param error: userData must be addr ptr to empty json object");
        return -1;
    }

    string stmt;
    if(0 <0){
        stmt = "select sn, active, updatetime, lastboot from info;";
    }else{
        stmt = "select sn, active, updatetime, lastboot from info where active="+to_string(0) +";";
    }
   
    pdb = exec(info, fileName, stmt.c_str(), _getInfo);
    if(sqlite3_errcode(pdb) != SQLITE_OK) {
        spdlog::error("failed to get info to file {}: {}", fileName, sqlite3_errmsg(pdb));
        return sqlite3_errcode(pdb);
    }

    spdlog::debug("getInfo to file {}: {}", fileName, v->dump());

    return 0;
}




// log: id integer, module text, type text, status integer, reported integer, content text, updatetime
//  eg: 1, ILSEVMGR1:0:0, alarm, 1, 0, "{data: low memory}", ts
// type = none|alarm|event


}