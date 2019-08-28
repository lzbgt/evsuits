// g++ -std=c++1z test_database.cpp database.cpp -o test_database -lleveldb -Ivendor/include/ -Iinc -Lvendor/lib
#include "inc/database.h"
#include "spdlog/spdlog.h"

int main(){
    json j;
    int ret = 0;
    // sn
    ret = LVDB::delValue(LVDB_KEY_SN, LVDB_FILE_GENERAL);
    spdlog::info("ret: {}", ret);

    ret = LVDB::getSn(j, LVDB_FILE_GENERAL);
    spdlog::info("ret2: {}, {}", ret, j.dump());

    ret = LVDB::setSn(j, LVDB_FILE_GENERAL);
    spdlog::info("ret: {}", ret);

    j.clear();

    ret = LVDB::getSn(j, LVDB_FILE_GENERAL);
    spdlog::info("ret: {}, {}", ret, j.dump());


    j.clear();
    spdlog::info("ret: {}", ret);
    ret = LVDB::getLocalConfig(j, LVDB_FILE_GENERAL);
    spdlog::info("ret1: {}, {}", ret, j.dump());

    ret= LVDB::setLocalConfig(j, LVDB_FILE_GENERAL);
    spdlog::info("ret2: {}, {}", ret, j.dump());

    ret = LVDB::getLocalConfig(j, LVDB_FILE_GENERAL);
    spdlog::info("ret3: {}, {}", ret, j.dump());


    return 0;
}