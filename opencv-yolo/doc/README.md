# 视频分析接口设计

### 子系统组成与交互
- mgr-svc: 管控中心后端
- video-crud-svc: 视频文件CRUD微服务, 包括: 事件视频上传微服务, 视频查询微服务, 视频录制微服务
- videoML-API-svc: 视频分析restful api
- videoML-Worker: 视频分析异步任务处理器
- azure-storage: 视频云存储

![sd-video-ml](videml-sequce.png)

### 接口定义
- [videoML-API-svc API spec (OpenAPI 3.0)](video.ai.api.yml)
- [videoML-API-svc MQTT spec](video.ai.mq.yml)


### 产品业务流程
1. **mgr-svc** 通过调用 videoML-API-svc的 va_config接口(/config_ipc)设定一组摄像头**开启**或**关闭**哪些视频分析功能
在 video.ai.api.yml里可以看到 post body.
2. **video-crud-svc** 在生成视频后向MQTT topic **/video.ai/v1.0/task** 发送视频分析任务, 并在数据库中将此视频对应记录的 **va_result**空object中加入属性**vaStartTime=ts**
3. **videoML** 系统收到此任务进行处理, 并将结果通过MQTT topic **/video.ai/v1.0/result** 通知video-crud-svc
4. **video-crud-svc** 收到 **/video.ai/v1.0/result**发布消息后将对应的视频记录的**va_result**字段增加property **result= [result]**, 同时记录结束事件 **vaEndTime=ts**
5. 前端业务部分:
   1. 在query视频返回后, 
      1. 如果**va_result**部分是个**空json**, 表明这个视频是整套系统部署之前的. 可以不用做任何特殊处理. 也可以加一个小context 按钮: “进行分析” (调用task http接口, 在api spec里)
      2. 如果va_result部分只有**vaStartTime=ts**, 表明这个视频还在分析过程中, 可以在对应视频的UI处加合适的挂件装饰.(正在分析中)
      3. 如果va_result部分有**result=object**, **vaStartTime**, **vaEndTime**, 则表明视频分析完成
         1. 通过**result.code, result.msg** 判断分析是否成功及原因.
         2. 通过**result.data**来决定如何显示视频.
            1. 如果result.data.huamanDetect.found == true, 给该视频的UI处增加一个小人装饰, 
            2. 如果result.data.xyzDetect.found == true, 给该视频的UI处增加一个xyz对应的装饰,
            3. 如果没有任何检测结果是true: 则在该视频UI处显示删除按钮装饰.


问1: 为什么不直接删除视频, 如果没有检测到物体?

答1: 因为目前不确定算法是否可靠. 并且物体检测只是AI的一个方面, 不能仅仅从这个方面来决定是否删除. AI检测只是提供更多关于视频的内容信息, 删不删除由产品业务决定. 不应该在基础服务里做这样的决定, 基础服务提供删除接口即可.