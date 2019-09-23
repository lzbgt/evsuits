#include "dirmon.h"
using namespace std;

using namespace fsw;

void fileEventHandler(const std::vector<event>&evts, void *pUser)
{
    for(auto&i:evts) {
        spdlog::info("path: {}, time: {}", i.get_path(), i.get_time());
    }
}

int CreateDirMon(monitor **m, string path, string ext, vector<string> &&events, FSW_EVENT_CALLBACK cb)
{
    vector<string> paths= {path};
    vector<fsw_event_type_filter> event_filters = {{fsw_event_flag::Created}};
    vector<monitor_filter> flt = {{.text=".*\\.mp4", .type=fsw_filter_type::filter_include, .case_sensitive = false, .extended=false}};
    *m = monitor_factory::create_monitor(
             fsw_monitor_type::system_default_monitor_type,
             paths,
             fileEventHandler);
    (*m)->set_latency(1.1);
    (*m)->set_filters(flt);
    (*m)->start();
    return 0;
}

int CloseDirMon()
{
    return 0;
}

#define DEBUG

#ifdef DEBUG

int main()
{
    monitor *mon = nullptr;
    CreateDirMon(&mon, "./slices", ".mp4", vector<string>(), nullptr);
}


#endif