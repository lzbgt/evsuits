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
#include <json.hpp>

using namespace std;
using json = nlohmann::json;

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
                spdlog::error("sqlite3_open: {}",sqlite3_errmsg(pdb));
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
            spdlog::error("sqlite3_exec: {}",sqlite3_errmsg(pdb));
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

int _getSn(void *pUser, int cc, char **cv, char **cn)
{
    int ret = SQLITE_OK;
    auto v = static_cast<string*>(pUser);
    if(cc == 1) {
        *v = string(cv[0]);
    }
    else {
        if(ret < 1) {
            ret = -1;
        }
        else {
            ret = 1;
        }
    }
    return ret;
}

int setSn(const char *sn, const char *fileName)
{
    int ret = 0;
    return ret;
}

int setLocalConfig(json config, const char* fileName)
{
    int ret = 0;
    string stmt;
    sqlite3* pdb =NULL;
    // init tables
    stmt = "create table if not exists info(cls text, value text, version text, update datetime, primary key cls);";
    pdb = exec(NULL, fileName, stmt.c_str(), NULL);
    if(sqlite3_errcode(pdb) != SQLITE_OK) {
        spdlog::error("failed to create table info: {}", sqlite3_errmsg(pdb));
        return -1;
    }
    // if sn exist
    string sn;
    stmt = "select value from info where cls=sn;";
    pdb = exec(&sn, fileName, stmt.c_str(), _getSn);
    if(sqlite3_errcode(pdb) != SQLITE_OK ||sn.empty()) {
        spdlog::error("failed get sn: {}, will create new one", sqlite3_errmsg(pdb));
        sn = genStrRand(8);
        stmt = "insert into info(cls, value, update) values(sn," + sn + ",'now');";
        if(sqlite3_errcode(pdb) != SQLITE_OK) {
            spdlog::error("failed insert sn: {}, will create new one", sqlite3_errmsg(pdb));
            return -1;
        }
    }

    return ret;
}

int getLocalConfig(json config)
{
    int ret = 0;
    return ret;
}

int _getSlices(void *pUser, int cc, char **cv, char **cn)
{
    int ret = 0;
    auto v = static_cast< vector<int>* >(pUser);
    if(cc != v->size()) {
        return SQLITE_ERROR;
    }
    else {
        for(int i = 0; i < v->size(); i ++) {
            v->at(i) = atoi(cv[0]);
        }
    }

    return ret;
}

// table and schema

// modules: id integer, pid integer, iid integer, cls text, sn text, config text, online integer, enabled integer, update
// eg: 2, 0, NULL, evmgr, ILSEVMGR1, "xxxx", 1, 1, ts
//   : 3, 2, NULL, ipc, NULL, "xxx", 1, 1, ts
//   : 5, 3, 1, evpuller, "ILSEVPULLER1", "xxx", 1, 1, ts
// cls = evmgr|ipc|evpuller|evpusher|evslicer|evmlmotion


int getSlices(void *pUser, int iid, const char *fileName)
{
    int ret = 0;
    sqlite3 * pdb = NULL;
    auto v = static_cast< vector<int>* >(pUser);
    string stmt = "select ts from slices where iid="+to_string(iid)+" order by id;";
    pdb = exec(pUser, fileName, stmt.c_str(), _getSlices);
    if(sqlite3_errcode(pdb) != SQLITE_OK) {
        // create
        stmt = "create table if not exists slices(id integer, iid integer, ts integer);";
        pdb = exec(NULL, fileName, stmt.c_str(), NULL);
        if(sqlite3_errcode(pdb) != SQLITE_OK) {
            spdlog::error("failed create table slices for evslicer {}", iid);
            return -1;
        }
        else {
            for(int i = 1; i <= v->size(); i ++) {
                stmt = "insert into slices(id, iid, ts) values(" + to_string(i) + to_string(iid) + ", 0";
                pdb = exec(NULL, fileName, stmt.c_str(), NULL);
                if(sqlite3_errcode(pdb) != SQLITE_OK) return -2;
                v->push_back(0);
            }

            stmt = "update slices set ts=1 where id = 1 and iid="+to_string(iid) +";";
            pdb = exec(NULL, stmt.c_str(), NULL, NULL);
            if(sqlite3_errcode(pdb) != SQLITE_OK) return -3;
            v->at(0) = 2;
        }
    }
    else {

    }

    return ret;
}
}