# Webnote

基于linux下webserver后端的个人备忘录，为了满足个人TODOlist的跨设备访问，以及熟悉linux后端开发细节。  
1.使用线程池+非阻塞socket+epoll(ET)+Reactor实现并发模型  
2.使用主从状态机解析HTTP请求报文，目前支持GET、POST、OPTIONS  
3.访问数据库实现用户注册、登录功能，可以请求图片与文件
## 框架
![structure](structure.jpg)
## Demo演示
![demo](demo.gif)