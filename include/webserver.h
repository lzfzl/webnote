#pragma once
#include <sys/socket.h>
#include<sys/epoll.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include <cstdio>
#include <cerrno>
#include <unordered_map>
#include "httphandler.h"
#include "workthread.h"
const int MAX_EVENT_NUMBER = 1024;
const int MAX_CONNECT_NUMBER = 10;
const int MAX_WORKER_NUMBER = 4;
class webserver{
public:
    webserver();
    void eventListen();
    void eventAccept();
private:
    int listenfd;
    int epollfd;
    threadpoll tp;
    struct epoll_event events[MAX_EVENT_NUMBER];
    void dealWithConn();
    void dealWithRead(int clientfd);
    void dealWithWrite(int clientfd);

    std::unordered_map<int,http_handler*> conn;
};