#include "inc/database.h"
#include <stdio.h>
#include <stdlib.h>
#include <mutex>
#include <map>

using namespace std;

namespace DB {
    map<string, sqlite3 *> maphdb;
    map<string, mutex> mapMut;

    int exec(char* fileName, const char* stmt, callback cb, void *pUserData){
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
                    printf("sqlite3_open:%s\n",sqlite3_errmsg(ppdb));
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
                printf("error: sqlite3_exec:%s\n",sqlite3_errmsg(ppdb));
            }
        }
        
        //sqlite3_close(ppdb);
        return ret;
    }
}