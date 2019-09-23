
## build libs
- LIBZMQ, LEVELDB
  ```
  make zmq leveldb
  ```
- FFMPEG
  ```
  offical ref:
  ```
- OPENCV
  ```
  offical ref:
  ```
- CURL
  ```
  offical ref:
  ```
- fswatch
  ```
  offical ref:
  ```

## build executables
```
make -j
```

## deploy edge & cloud systems
- TODO: use systemd or supervisord to auto start on boot and monitoring
- manually: evdaemon will start all other 5 subsystems(evmgr, evpuller, evpusher, evmlmotion, evslicer) on a edge device.
  ```
  # on edge device
  # environments: BOOTSTRAP=true(default)|fase, DAEMON_PORT(8088, http), ROUTER_PORT(5549, tcp) 
  $ CLOUD_ADDR=tcp://<EVCLOUDSVC_IP>:<EVCLOUDSVC_PORT> path_to/evdaemo

  # on cloud
  # environments: MSG_PORT(5548, tcp), HTTP_PORT(8089, http)
  $ MSG_PORT=<EVCLOUDSVC_PORT> evcloudsvc
  ```

## configuration


## trouble-shooting
```
TODO:
```