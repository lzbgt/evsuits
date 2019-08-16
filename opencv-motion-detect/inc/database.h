#ifndef __DATABASE_SL__
#define __DATABASE_SL__
extern "C" {
    #include "../vendor/sqlite/sqlite3.h"
}

namespace DB {
    typedef     int (*callback)(void*,int,char**,char**);
    int exec(void *pUserData, char* fileName, const char* stmt, callback cb);
    int getSlices(void *pUser, int iid, const char *fileName);
}

#endif