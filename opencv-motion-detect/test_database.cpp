// g++ -std=c++1z test_database.cpp database.cpp -o test_database -lleveldb -Ivendor/include/ -Iinc -Lvendor/lib
#include "inc/database.h"
#include "spdlog/spdlog.h"

const char *_config = "{\"time\":0,\"code\":0,\"data\":{\"ILSEVMGR1\":{\"sn\":\"ILSEVMGR1\",\"addr\":\"172.31.0.76\",\"addr-cloud\":\"172.31.0.76\",\"proto\":\"zmq\",\"port-cloud\":5556,\"port-router\":5550,\"status\":1,\"ipcs\":[{\"addr\":\"172.31.0.51\",\"proto\":\"rtsp\",\"user\":\"admin\",\"password\":\"FWBWTU\",\"status\":1,\"modules\":{\"evpuller\":[{\"sn\":\"ILSEVPULLER1\",\"addr\":\"172.31.0.76\",\"iid\":1,\"port-pub\":5556,\"status\":1}],\"evpusher\":[{\"sn\":\"ILSEVPUSHER1\",\"iid\":1,\"urlDest\":\"rtsp://40.73.41.176:554/test1\",\"user\":\"\",\"password\":\"\",\"token\":\"\",\"enabled\":1,\"status\":1}],\"evslicer\":[{\"sn\":\"ILSEVSLICER1\",\"iid\":1,\"path\":\"slices\",\"enabled\":1,\"status\":1}],\"evml\":[{\"type\":\"motion\",\"sn\":\"ILSEVMLMOTION1\",\"iid\":1,\"enabled\":1,\"status\":1}]}}]}}}";

int main(){
    json j;
    int ret = 0;
    // sn
    ret = LVDB::delValue(LVDB_KEY_SN, LVDB_FILE_GENERAL);
    spdlog::info("ret: {}", ret);
    ret = LVDB::setSn(j, LVDB_FILE_GENERAL);
    spdlog::info("ret: {}", ret);

    j["sn"] = "snaaaa";
    j["lastboot"] = "2019-08-25 10:10:10";
    j["updatetime"] = "2019-08-25 10:10:11";
    ret = LVDB::setSn(j, LVDB_FILE_GENERAL);
    spdlog::info("ret: {}", ret);

    j.clear();

    ret = LVDB::getSn(j, LVDB_FILE_GENERAL);
    spdlog::info("ret: {}, {}", ret, j.dump());

    //
    json config = json::parse(_config);

    j.clear();
    ret = LVDB::delValue(LVDB_KEY_CONFIG, LVDB_FILE_GENERAL);
    spdlog::info("ret: {}", ret);
    ret = LVDB::getLocalConfig(j, LVDB_FILE_GENERAL);
    spdlog::info("ret: {}, {}", ret, j.dump());

    ret= LVDB::setLocalConfig(j, LVDB_FILE_GENERAL);
    spdlog::info("ret: {}, {}", ret, j.dump());

    ret= LVDB::setLocalConfig(config, LVDB_FILE_GENERAL);
    spdlog::info("ret: {}, {}", ret, config.dump());

    ret = LVDB::getLocalConfig(j, LVDB_FILE_GENERAL);
    spdlog::info("ret: {}, {}", ret, j.dump());


    return 0;
}