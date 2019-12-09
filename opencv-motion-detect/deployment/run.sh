#!/bin/sh
if [ "$#" -lt 2 ]; then 
    echo "usage: <path_to_video_file>  <simutate_IPC_SN> [rtsp_server_addr]"
    exit 1
fi

if [ "$#" -eq 3 ]; then
    ffmpeg -stream_loop -1 -re -i $1 -codec copy -rtsp_transport tcp -r 18 -f rtsp $3/$2
else
    ffmpeg -stream_loop -1 -re -i $1 -codec copy -rtsp_transport tcp -r 18 -f rtsp rtsp://qz.videostreaming.ilabservice.cloud/$2
fi