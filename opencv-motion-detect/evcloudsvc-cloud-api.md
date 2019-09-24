### evcloudsvc restful api

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
    "code": 0,
    "data": {
        "NMXH73Y2": {
            "addr": "127.0.0.1",
            "api-cloud": "http://127.0.0.1:8089",
            "ipcs": [
                {
                    "addr": "172.31.0.51",
                    "modules": {
                        "evml": [
                            {
                                "area": 300,
                                "enabled": 1,
                                "iid": 1,
                                "post": 30,
                                "pre": 3,
                                "sn": "NMXH73Y2",
                                "thresh": 80,
                                "type": "motion"
                            }
                        ],
                        "evpuller": [
                            {
                                "addr": "127.0.0.1",
                                "enabled": 1,
                                "iid": 1,
                                "port-pub": 5556,
                                "sn": "NMXH73Y2"
                            }
                        ],
                        "evpusher": [
                            {
                                "enabled": 1,
                                "iid": 1,
                                "password": "",
                                "sn": "NMXH73Y2",
                                "token": "",
                                "urlDest": "rtsp://40.73.41.176:554/test1",
                                "user": ""
                            }
                        ],
                        "evslicer": [
                            {
                                "enabled": 1,
                                "iid": 1,
                                "path": "slices",
                                "sn": "NMXH73Y2"
                            }
                        ]
                    },
                    "password": "FWBWTU",
                    "port": 554,
                    "proto": "rtsp",
                    "user": "admin"
                }
            ],
            "mqtt-cloud": "<cloud_addr>",
            "port-cloud": 5556,
            "port-router": 5550,
            "proto": "zmq",
            "sn": "NMXH73Y2"
        }
    },
    "msg": "ok"
}
```


#### POST /config
##### description
set or change configuration for edge device
##### params
- none
##### return
- type: json
- example:
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

#### GET /sysinfo
##### description
*[NOT IMPLEMENTED]* get edge terminal hw & os infomation including resource usage of CPU, RAM, IO, DISK etc...

#### POST /debug
##### description
*[NOT IMPLEMENTED]* turn on/off debug tunnel
##### params
- sn: string
- op: on|off
- ip: string. ip of public accessable host
- port(optional): number. ssh port of the public accessable host. default 22.
- user: ssh user of the public accessable host
- password: ssh password of the public accessable host
##### return
- type: json
- example
```
{"code":0, "msg":"ok"}
```


