#pragma once
#include <sys/socket.h>
#include<sys/epoll.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include <cstdio>
#include <cerrno>
#include <unordered_map>
#include "httphandler.h"
const int MAX_EVENT_NUMBER = 1024;
const int MAX_CONNECT_NUMBER = 10;
class webserver{
public:
    int listenfd;
    int epollfd;
    struct epoll_event events[MAX_EVENT_NUMBER];
    webserver();
    void eventListen();
    void dealWithConn();
    void dealWithRead(int clientfd);
    void dealWithWrite(int clientfd);
    void eventAccept();
    std::unordered_map<int,http_handler*> conn;
};