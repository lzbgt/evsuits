#include <iostream>
#include <vector>
#include <map>
#include <list>
#include <algorithm>
#include "inc/fs.h"
#include "inc/spdlog/spdlog.h"

using namespace std;


int test_last_write_time(){
    fs::path p = fs::current_path() / "example.bin";
    std::ofstream(p.c_str()).put('a'); // create file
    auto ftime = fs::last_write_time(p);

    // assuming system_clock for this demo
    // note: not true on MSVC or GCC 9; C++20 will allow portable output
    std::time_t cftime = decltype(ftime)::clock::to_time_t(ftime);
    std::cout << "File write time is " << std::asctime(std::localtime(&cftime)) << '\n';

    fs::last_write_time(p, ftime + 1h); // move file write time 1 hour to the future
    ftime = fs::last_write_time(p); // read back from the filesystem

    cftime = decltype(ftime)::clock::to_time_t(ftime);
    std::cout << "File write time is " << std::asctime(std::localtime(&cftime)) << '\n';
    fs::remove(p);

    return 0;
}

void ftime2ctime(fs::file_time_type ftime){
    std::time_t cftime = decltype(ftime)::clock::to_time_t(ftime);
    std::cout << "\t\twt: " << std::asctime(std::localtime(&cftime)) << std::endl; 
}

int LoadVideoFiles(string path, map<long, string> &ts2fileName, list<long> &tsRing) {
    int ret = 0;
    try{
        for (const auto & entry : fs::directory_iterator(path))
        {
            if(entry.file_size() == 0 || !entry.is_regular_file()) {
                spdlog::warn("LoasdVideoFiles skipped {} (empty or directory)", entry.path().c_str());
            }
            auto ftime = fs::last_write_time(entry.path());
            auto ts = decltype(ftime)::clock::to_time_t(ftime);
            spdlog::debug("ts: {}, file: {}", ts, entry.path().c_str());
            if(ts2fileName.count(ts) != 0) {
                spdlog::warn("LoasdVideoFiles multiple files with same timestamp: {}, {}(skipped), ", ts2fileName[ts], entry.path().c_str());
                continue;
            }
            tsRing.insert(std::lower_bound(tsRing.begin(), tsRing.end(), ts), ts);
            ts2fileName[ts] = entry.path();
        }
        // for(auto &i: tsRing) {
        //     spdlog::debug("ts: {}, file: {}", i, ts2fileName[i]);
        // }
        
    }catch(exception &e) {
        spdlog::error("LoasdVideoFiles exception : {}", e.what());
        ret = -1;
    }

    return ret;
}

int main(int argc, const char *argv[]) {

    std::string path = argv[1];
    list<long> tsRing;
    map<long, string> ts2fileName;
    LoadVideoFiles(path, ts2fileName, tsRing);

    return 0;
}