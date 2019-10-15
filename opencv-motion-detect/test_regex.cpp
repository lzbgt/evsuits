#include <iostream>
#include <regex>
#include <iterator>
#include <vector>
#include <string>
#include <fstream>
#include <set>
#include "json.hpp"
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include "utils.hpp"

using namespace std;
using namespace nlohmann;

int main(){
    std::ifstream iff("deployment/config.json");
    std::ifstream iff2("deployment/config_copy.json");
    json config, config2;
    iff >> config;
    config = config["data"];
    iff2 >> config2;
    config2 = config2["data"];
    json dif = json::diff(config2, config);
    spdlog::info("diff: {}", dif.dump());
    auto ret = cfgutils::getModulesOperFromConfDiff(config2, config, dif, "");
    spdlog::info("parse: {}", ret.dump());
}