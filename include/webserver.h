#pragma once
#include <sys/socket.h>
#include<sys/epoll.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include <cstdio>
#include <cerrno>
#include <unordered_map>
#include <sys/timerfd.h>
#include "httphandler.h"
#include "workthread.h"
#include "timer.h"
#include "sql_conn_pool.h"
const int MAX_EVENT_NUMBER = 1024;
const int MAX_CONNECT_NUMBER = 1024;
class webserver{
public:
    webserver();
    void eventListen();
    void setTimer();
    void eventAccept();
    ~webserver();
private:
    sqlPools sp;
    timer timedeal;
    int listenfd;
    int epollfd;
    int m_timerfd;
    threadpoll tp;
    struct epoll_event events[MAX_EVENT_NUMBER];
    void dealWithConn();
    void dealWith(int clientfd);
    http_handler conn[MAX_CONNECT_NUMBER];
};