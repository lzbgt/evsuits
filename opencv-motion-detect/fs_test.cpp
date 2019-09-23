#include <iostream>
#include <vector>
#include <map>
#include <list>
#include <algorithm>
#include "inc/fs.h"
#include "inc/spdlog/spdlog.h"

using namespace std;


int test_last_write_time()
{
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

void ftime2ctime(fs::file_time_type ftime)
{
    std::time_t cftime = decltype(ftime)::clock::to_time_t(ftime);
    std::cout << "\t\twt: " << std::asctime(std::localtime(&cftime)) << std::endl;
}

vector<long> LoadVideoFiles(string path, int days, map<long, string> &ts2fileName, list<long> &tsRing, list<long> &tsNeedProc)
{
    vector<long> v;
    // get current timestamp

    auto now = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
    try {
        for (const auto & entry : fs::directory_iterator(path)) {
            if(entry.file_size() == 0 || !entry.is_regular_file()||entry.path().extension() != ".mp4") {
                spdlog::warn("LoasdVideoFiles skipped {} (empty/directory/!mp4)", entry.path().c_str());
                continue;
            }
            auto ftime = fs::last_write_time(entry.path());
            auto ts = decltype(ftime)::clock::to_time_t(ftime);

            // check if processed already
            if(ts2fileName.count(ts) != 0) {
                spdlog::warn("LoasdVideoFiles multiple files with same timestamp: {}, {}(skipped), ", ts2fileName[ts], entry.path().c_str());
                continue;
            }

            // check old files
            if(ts - now > days * 24 * 60 * 60) {
                spdlog::info("file {} old that {} days", entry.path().c_str(), days);
                tsNeedProc.push_back(ts);
            }
            else {
                tsRing.insert(std::lower_bound(tsRing.begin(), tsRing.end(), ts), ts);
            }

            // add to map
            ts2fileName[ts] = entry.path();
        }
    }
    catch(exception &e) {
        spdlog::error("LoasdVideoFiles exception : {}", e.what());
    }

    return v;
}

int main(int argc, const char *argv[])
{

    std::string path = argv[1];
    list<long> tsRing;
    list<long> tsProcess;
    map<long, string> ts2fileName;

    LoadVideoFiles(path, 2, ts2fileName, tsRing, tsProcess);

    for(auto &i:tsRing) {
        spdlog::info("tsRing: {} file: {}", i, ts2fileName[i]);
    }

    return 0;
}