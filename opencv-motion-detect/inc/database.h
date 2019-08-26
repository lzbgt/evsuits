#ifndef __DATABSE_LEVEL_DB_H__
#define __DATABSE_LEVEL_DB_H__
#include "leveldb/db.h"
#include "json.hpp"

using namespace nlohmann;
using namespace std;

namespace LVDB {
    int getSn(json &info, string fileName);
    int saveSn(json &info, string fileName);
    int loadLocalConfig(json &config, string fileName);
    int savelocalConfig(json &config, string fileName);
}

#endif