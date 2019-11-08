### evcloudsvc restful api
RFC 6902 JSON Patch
- java: https://github.com/flipkart-incubator/zjsonpatch
- c++: https://github.com/nlohmann/json

#### GET /config
##### description
query configuration for edge device with specified sn
##### params
- sn: string. device serial
##### return
- type: json
- field "code": 0 - success; otherwise failed.
- field "msg": string, readable string for "code"
- field "data": configuration for sn.
- example
```
{
   "data":{
      "V2I0C7KC":{
         "addr":"127.0.0.1",
         "apiCloud":"http://127.0.0.1:8089",
         "ipcs":[
            {
               "addr":"172.31.0.129",
               "modules":{
                  "evml":[
                     {
                        "area":200,
                        "enabled":1,
                        "iid":1,
                        "post":30,
                        "pre":4,
                        "sn":"V2I0C7KC",
                        "thresh":30,
                        "fpsProc": 3,
                        "entropy": 0.3,
                        "type":"motion"
                     }
                  ],
                  "evpuller":[
                     {
                        "addr":"127.0.0.1",
                        "iid":1,
                        "enabled": 1,
                        "portPub":5556,
                        "sn":"V2I0C7KC"
                     }
                  ],
                  "evpusher":[
                     {
                        "enabled":1,
                        "iid":2,
                        "password":"",
                        "sn":"V2I0C7KC",
                        "token":"",
                        "urlDest":"rtsp://40.73.41.176/V2I0C7KC",
                        "user":""
                     }
                  ],
                  "evslicer":[
                     {
                        "enabled":1,
                        "iid":1,
                        "path":"slices",
                        "sn":"V2I0C7KC",
                        "videoServerAddr": "http://40.73.41.176:10009/upload/evtvideos/"
                     }
                  ]
               },
               "password":"iLabService",
               "port":554,
               "proto":"rtsp",
               "user":"admin",
               "sn": "V2I0C7KC"
            }
         ],
         "mqttCloud":"<cloud_addr>",
         "portCloud":5556,
         "portRouter":5550,
         "proto":"zmq",
         "sn":"V2I0C7KC"
      }
   },
   "lastupdated":1567669674
}
```


#### POST /config
##### description
set or change configuration for edge device
##### params
- patch(optional): false(default)|true, wheather body is the json diff regards to edge device identified by sn
- sn: string, only used when patch is set as true
##### body
- type: json
- example
1. full configure
```
{
   "data":{
      "NMXH73Y2":{
         "addr":"127.0.0.1",
         "api-cloud":"http://127.0.0.1:8089",
         "ipcs":[
            {
               "addr":"172.31.0.51",
               "modules":{
                  "evml":[
                     {
                        "area":300,
                        "enabled":1,
                        "iid":1,
                        "post":30,
                        "pre":3,
                        "sn":"NMXH73Y2",
                        "thresh":80,
                        "type":"motion"
                     }
                  ],
                  "evpuller":[
                     {
                        "addr":"127.0.0.1",
                        "iid":1,
                        "enabled": 1,
                        "port-pub":5556,
                        "sn":"NMXH73Y2"
                     }
                  ],
                  "evpusher":[
                     {
                        "enabled":1,
                        "iid":1,
                        "password":"",
                        "sn":"NMXH73Y2",
                        "token":"",
                        "urlDest":"rtsp://40.73.41.176:554/test1",
                        "user":""
                     }
                  ],
                  "evslicer":[
                     {
                        "enabled":1,
                        "iid":1,
                        "path":"slices",
                        "sn":"NMXH73Y2"
                     }
                  ]
               },
               "password":"FWBWTU",
               "port":554,
               "proto":"rtsp",
               "user":"admin"
            }
         ],
         "mqtt-cloud":"<cloud_addr>",
         "port-cloud":5556,
         "port-router":5550,
         "proto":"zmq",
         "sn":"NMXH73Y2"
      }
   },
   "lastupdated":1567669674
}
```
2. patch configure (POST /config?patch=true&sn=NMXH73Y2)
   - TLDR: SN of the path in patch may not be same with the sn in params, since a module on this device may connect to its cluster mgr on another deivce. Device configure is a logic view of the edge cluster (viewpoint from the cluster mgr): 
     - if a device runs a cluster mgr, then it has a full configuration with its sn as key and all other configurations of releated submodules.
     - if a device doest not run a cluster mgr, but only submodule(s), it will reuse(refer to) the configuration(s) of the cluster mgr(s) that must be running other device(s) which the submodule(s) shall connect to.
```
[{"op":"add","path":"/RBKJ62Z1/ipcs/0/modules/evpuller/0/enabled","value":1}]
```
##### return
- type: json
- example:
```
{"code": 0, "msg":"ok", "data":JSON}
```

#### GET /keys
##### description
query all keys in cloud db
##### params
- none
##### return
- type: json array
- example
```
[
    "NMXH73Y2",
    "NMXH73Y2_bak",
    "SN",
    "configmap",
    "configmap_bak"
]
```
#### GET /value
##### description
get value for specified key in cloud db. keys list is queried by /keys api
##### params
- key: string
##### return
- type: json
- example
```
# GET /value?key=configmap
{
    "NMXH73Y2": "NMXH73Y2",
    "code": 0,
    "mod2mgr": {
        "NMXH73Y2:evml:motion": "NMXH73Y2",
        "NMXH73Y2:evpuller": "NMXH73Y2",
        "NMXH73Y2:evpusher": "NMXH73Y2",
        "NMXH73Y2:evslicer": "NMXH73Y2"
    },
    "sn2mods": {
        "NMXH73Y2": [
            "NMXH73Y2:evml:motion",
            "NMXH73Y2:evpuller",
            "NMXH73Y2:evpusher",
            "NMXH73Y2:evslicer"
        ]
    }
}
```
#### POST /factory-reset
##### description
*[NOT IMPLEMENTED]* total reset edge terminal
##### params
- sn: string
##### return
- type: json
- example
```
{"code": 0, "msg":"ok", ...}
```

#### GET /ipcstatus
##### description
get status of ipc camera(s)



#### GET /sysinfo
##### description
*[NOT IMPLEMENTED]* get edge terminal hw & os infomation including resource usage of CPU, RAM, IO, DISK etc...

#### POST /cmd
##### description
send cmd to edge.
currently implemented cmd: reversetun, debug:list_files, debug:record, debug:toggle_log
##### cmd: reversetun
###### description
create reverse ssh tunnel between edge box and an sshd server
###### params
none
###### body
```
{
   "target":"0017SRTC",
   "metaType":"cmd",
   "metaValue":"reversetun",
   "data":{
      "host":"47.56.83.236",
      "user":"root",
      "password":"Hz123456",
      "port":9999
   }
}
```
- target: edge box SN
- metaType: always being "cmd"
- metaValue: "reversetun
- data.host: cloud sshd host (with serving port on 22)
- data.user: cloud sshd user name
- data.password: cloud sshd password
- data.port: port on cloud sshd host, which will be created and reversed tunneling to the port 22 of the edge box.

###### return
- type: json
- example
```
{"code":0, "msg":"ok"}
```