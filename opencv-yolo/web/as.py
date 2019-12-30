#!/usr/bin/env python
# coding: utf-8

from azure.storage.fileshare.aio import ShareFileClient
from azure.storage.fileshare import ShareDirectoryClient, ShareServiceClient
import asyncio, re, datetime, queue, json, threading
from concurrent.futures import ThreadPoolExecutor

CONNSTR='DefaultEndpointsProtocol=https;AccountName=ilsvideostablediag;AccountKey=rWeA/cUiWAsDqGHO0lfDB5eDHNZxCChrH0pMvICdNJR6tt+hE2tHlSl9kUEjqyOY6cztPWaaRbbeoI47uNEeWA==;EndpointSuffix=core.chinacloudapi.cn'
SHARENAME='pre-data'

event_loop = asyncio.get_event_loop()
tasks = []
videoQue = queue.Queue()

async def downloadFile(ipcSn, dirName, fileName, q):
    async with ShareFileClient.from_connection_string(conn_str=CONNSTR, share_name=SHARENAME, file_path=ipcSn+'/'+dirName+'/'+fileName) as fc:
        with open(fileName, "wb") as f:
            data = await fc.download_file()
            await data.readinto(f)
            q.put({'f': fileName, 'sn': ipcSn, 'd': dirName})

def analyzeFile(q):
    while True:
        try:
            job = q.get(timeout = 60*60)
            if job:
                print(json.dumps(job))
                
            else:
                print("none")
        except Exception as e:
            print('timeout get job')

def main(q):
    rootc = ShareDirectoryClient.from_connection_string(conn_str=CONNSTR, share_name=SHARENAME, directory_path=".")
    rootd = rootc.list_directories_and_files()

    # download files
    fileCnt = 0
    totalSize = 0
    totalTime = 0
    bBreak = False
    for ipc in rootd:
        if bBreak: break
        if 'is_directory' in ipc and ipc['is_directory'] and ('-' not in ipc["name"]):
            ipcSn = ipc["name"]
            ipcc = rootc.get_subdirectory_client(ipc["name"])
            ipcd = ipcc.list_directories_and_files()
            for d in ipcd:
                if bBreak: break
                if 'is_directory' in d and d['is_directory']:
                    dirName = d["name"]
                    vf = ipcc.get_subdirectory_client(d["name"]).list_directories_and_files()
                    for e in vf:
                        if bBreak: break
                        m = re.match(r"(\d{13})-(\d{13}).mp4", e["name"])
                        if m:
                            fileName = e["name"]
                            fileSize = e["size"]
                            ts = int(int(m.group(1))/1000)
                            te = int(int(m.group(2))/1000)
                            delta = te - ts
                            print("{} {} {:.3f}MB".format(ipcSn, fileName, fileSize/1024/1024)) #datetime.datetime.fromtimestamp(te))
                            fileCnt+=1
                            if fileCnt >= 3:
                                bBreak = True;

                            now = datetime.datetime.now()
                            tasks.append(asyncio.ensure_future(downloadFile(ipcSn, dirName, fileName, videoQue), loop=event_loop))
    event_loop.run_until_complete(asyncio.gather(*tasks))

with ThreadPoolExecutor(max_workers = 4) as te:
    te.submit(main, videoQue)
    te.submit(analyzeFile, videoQue)




