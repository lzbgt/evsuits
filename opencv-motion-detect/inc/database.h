#ifndef __DATABASE_SL__
#define __DATABASE_SL__
extern "C" {
    #include "../vendor/sqlite/sqlite3.h"
}

namespace DB {
    typedef     int (*callback)(void*,int,char**,char**);
    int exec(char* fileName, const char* stmt, callback cb, void *pUserData);
}


#endif