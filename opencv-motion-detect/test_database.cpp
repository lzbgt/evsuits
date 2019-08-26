#include "inc/database.h"
#include "spdlog/spdlog.h"

int main(){
    int ret;
    json j, p;

    // case table info
    spdlog::set_level(spdlog::level::debug);
    
    ret = DB::clearTable("info", "aa.db");
    spdlog::info("ret: {}", ret);

    ret = DB::clearTable("info", "a.db");
    spdlog::info("ret: {}", ret);

    ret = DB::setInfo(&p, "a.db");
    spdlog::info("ret: {}", ret);

    j["sn"] = "sn-aa";
    j["lastboot"] = "2019-04-25 10:10:10";

    ret = DB::setInfo(&j, "a.db");
    spdlog::info("ret: {}", ret);

    ret = DB::getInfo(&j, -1, "a.db");
    spdlog::info("ret: {}", ret);

    j["sn"] = "sn-aa";
    j["lastboot"] = "2019-04-25 10:10:17";

    ret = DB::setInfo(&j, "a.db");
    spdlog::info("ret: {}", ret);

    ret = DB::getInfo(&j, -1, "a.db");
    spdlog::info("ret: {}", ret);


    j["sn"] = "sn-ab";
    j["lastboot"] = "2019-04-25 10:10:17";

    ret = DB::setInfo(&j, "a.db");
    spdlog::info("ret: {}", ret);

    ret = DB::getInfo(&p, -1, "a.db");
    spdlog::info("ret: {}", ret);

    p.clear();
    ret = DB::getInfo(&p, 0, "a.db");
    spdlog::info("ret: {}", ret);

    p.clear();
    ret = DB::getInfo(&p, 1, "a.db");
    spdlog::info("ret: {}", ret);


    // case table modules

    return 0;
}