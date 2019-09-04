#ifndef __EV_UTILS_H__
#define __EV_UTILS_H__

#include <vector>
#include <chrono>
#include <thread>
#include <map>
#include <sstream>
#include "json.hpp"
#include "spdlog/spdlog.h"

using namespace std;
using namespace nlohmann;
// cloudutils
namespace cloudutils
{
/**
 * scn: 
 *   evpuller, evmgr, evmotion, evslicer
 * 
 * */
/*
{
   "time":0,
   "lastupdated": 0,
   "code":0,
   "data":{
      "ILSEVMGR1":{
         "lastupdated": 0,
         "sn":"ILSEVMGR1",
         "addr":"172.31.0.76",
         "addr-cloud":"172.31.0.76",
         "proto":"zmq",
         "port-cloud":5556,
         "port-router":5550,
         "status":1,
         "ipcs":[
            {
               "addr":"172.31.0.51",
               "proto":"rtsp",
               "user":"admin",
               "password":"FWBWTU",
               "port": 554,
               "status":1,
               "modules":{
                  "evpuller":[
                     {
                        "sn":"ILSEVPULLER1",
                        "addr":"172.31.0.76",
                        "iid":1,
                        "port-pub":5556,
                        "status":1
                     }
                  ],
                  "evpusher":[
                     {
                        "sn":"ILSEVPUSHER1",
                        "iid":1,
                        "urlDest":"rtsp://40.73.41.176:554/test1",
                        "user":"",
                        "password":"",
                        "token":"",
                        "enabled":1,
                        "status":1
                     }
                  ],
                  "evslicer":[
                     {
                        "sn":"ILSEVSLICER1",
                        "iid":1,
                        "path":"slices",
                        "enabled":1,
                        "status":1
                     }
                  ],
                  "evml":[
                     {
                        "type":"motion",
                        "sn":"ILSEVMLMOTION1",
                        "iid":1,
                        "enabled":1,
                        "status":1
                     }
                  ]
               }
            }
         ]
      }
   }
}
*/
// const char *config = "{\"time\":0,\"code\":0,\"data\":{\"ILSEVMGR1\":{\"sn\":\"ILSEVMGR1\",\"addr\":\"127.0.0.1\",\"addr-cloud\":\"127.0.0.1\",\"proto\":\"zmq\",\"port-cloud\":5556,\"port-router\":5550,\"status\":1,\"ipcs\":[{\"addr\":\"172.31.0.51\",\"proto\":\"rtsp\",\"user\":\"admin\",\"password\":\"iLabService\",\"status\":1,\"modules\":{\"evpuller\":[{\"sn\":\"ILSEVPULLER1\",\"addr\":\"127.0.0.1\",\"iid\":1,\"port-pub\":5556,\"status\":1}],\"evpusher\":[{\"sn\":\"ILSEVPUSHER1\",\"iid\":1,\"urlDest\":\"rtsp://40.73.41.176:554/test1\",\"user\":\"\",\"password\":\"\",\"token\":\"\",\"enabled\":1,\"status\":1}],\"evslicer\":[{\"sn\":\"ILSEVSLICER1\",\"iid\":1,\"path\":\"slices\",\"enabled\":1,\"status\":1}],\"evml\":[{\"type\":\"motion\",\"sn\":\"ILSEVMLMOTION1\",\"iid\":1,\"enabled\":1,\"status\":1}]}}]}}}";

json registry(const char *sn, const json &config)
{
    // find local info in db
    // request cloud info
    // moc

    return json();
}

vector<string> split(const std::string& s, char delimiter)
{
   std::vector<std::string> tokens;
   std::string token;
   std::istringstream tokenStream(s);
   while (getline(tokenStream, token, delimiter))
   {
      tokens.push_back(token);
   }
   return tokens;
}


} // namespace cloudutils


#endif
