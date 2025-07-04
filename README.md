# SAM-based-Semantic-Segmentation-Server
Linux；SegmentAnything；EPOLL；Server
# 基于SAM语义分割大模型的客户端-服务器

## 效果图

![image](https://github.com/user-attachments/assets/19598b5a-df28-4354-b0b6-fba7af603c44)


在浏览器前端，用户可选择任意尺寸图像上传，并点击图像的任意位置作为分割的prompt，经由服务器SAM模型分割后返回分割结果。

## 框架介绍

IO多路复用 **EPOLL** 非阻塞 Socket +线程池等技术实现并发处理；解析 POST 和 GET 请求；异步/同步日志系统（TODO）；支持 webbench 进行压力测试（TODO），支持每秒近万发并发访问。分割算法采用 SegmentAnything（ViT-Base）。

## 运行

### 部署SAM模型

参考SAM官方

[facebookresearch/segment-anything: The repository provides code for running inference with the SegmentAnything Model (SAM), links for downloading the trained model checkpoints, and example notebooks that show how to use the model.](https://github.com/facebookresearch/segment-anything)

### 服务器部署

#### 首先编译

```c++
g++ -g main.cpp locker/locker.cpp threadpool/threadpool.h http_connection/http_cn.cpp -o main -pthread
```

#### 运行

设置端口号10000：

```
./main 10000
```

#### 在任意浏览器中，访问服务器资源

```
http://xxxx:10000/seg3.html
```

即可访问使用

## TODO

- 界面美化
- 压力测试
- 日志系统

