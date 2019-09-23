
## build libs
- LIBZMQ, LEVELDB
  ```
  make zmq leveldb
  ```
- FFMPEG
  ```
  ref:
  ```
- OPENCV
  ```
  ref:
  ```
- CURL
  ```
  ref:
  ```
- fswatch
  ```
  ref:
  ```

## build executables
```
make -j
```

## deploy edge & cloud systems
- TODO: systemd or supervisor auto for auto start on boot and monitoring
- manually: evdaemon will start all other 5 subsystems(evmgr, evpuller, evpusher, evmlmotion, evslicer) on a edge device.
  ```
  # on edge device
  $ CLOUD_ADDR=tcp://<EVCLOUDSVC_IP>:<EVCLOUDSVC_PORT> path_to/evdaemo

  # on cloud
  $ evcloudsvc
  ```

## configuration


## trouble-shooting
```
TODO:
```