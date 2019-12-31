
## example msg
- task
``` json
{
   "cameraId":"D72154040",
   "endTime":1577267418999,
   "image":"http://evcloudsvc.ilabservice.cloud/video/D72154040/1550143347000-1577267418999/firstFrame.jpg",
   "length":260,
   "startTime":1550143347000,
   "video":"http://evcloudsvc.ilabservice.cloud/video/D72154040/1550143347000-1577267418999/1550143347000-1577267418999.mp4"
}
```
- result
  - success
  ``` json
  {
   "code":0,
   "msg":"ok",
   "target":{
      "cameraId":"D72154040",
      "endTime":1577267418999,
      "image":"http://evcloudsvc.ilabservice.cloud/video/D72154040/1550143347000-1577267418999/firstFrame.jpg",
      "length":260,
      "startTime":1550143347000,
      "video":"http://evcloudsvc.ilabservice.cloud/video/D72154040/1550143347000-1577267418999/1550143347000-1577267418999.mp4"
   },
   "data":{
      "humanDetect":{
         "found":1,
         "level":"0.857669",
         "image":"detect_person_1577762816808.jpg"
      }
   }
  }
  ```
  - not found
  ``` json
  {
   "code":0,
   "msg":"ok",
   "target":{
      "cameraId":"D72154040",
      "endTime":1577267418999,
      "image":"http://evcloudsvc.ilabservice.cloud/video/D72154040/1550143347000-1577267418999/firstFrame.jpg",
      "length":260,
      "startTime":1550143347000,
      "video":"http://evcloudsvc.ilabservice.cloud/video/D72154040/1550143347000-1577267418999/1550143347000-1577267418999.mp4"
   },
   "data":{
      "humanDetect":{
         "found":0
      }
   }
  }
  ```
  - exception/error
  ``` json
  {
   "code":-1,
   "msg":"The specified parent path does not exist. RequestId:faef0312-401a-0001-4d9e-bfbae3000000 Time:2019-12-31T05:50:33.8073081Z ErrorCode:ParentNotFound Error:None",
   "target":{
      "cameraId":"D721540401",
      "endTime":1577267418999,
      "image":"http://evcloudsvc.ilabservice.cloud/video/D72154040/1550143347000-1577267418999/firstFrame.jpg",
      "length":260,
      "startTime":1550143347000,
      "video":"http://evcloudsvc.ilabservice.cloud/video/D72154040/1550143347000-1577267418999/1550143347000-1577267418999.mp4"
   }
  }
  ```