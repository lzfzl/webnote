#include "webserver.h"
webserver::webserver(){};
void webserver::eventListen(){
    listenfd = socket(AF_INET,SOCK_STREAM,0);
    if(listenfd<0){
        perror("socket");
        return;
    }
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);//able to accept 所有接口上的连接请求,4 bytes
    address.sin_port = htons(8080);//16 bits
    int ret = bind(listenfd,(struct sockaddr *)&address,sizeof(address));
    if(ret<0){
        perror("bind");
        return;
    }
    ret = listen(listenfd,5);
    if(ret<0){
        perror("listen");
        return;
    }
    epollfd = epoll_create1(EPOLL_CLOEXEC);
    if(epollfd<0){
        perror("epoll_create");
        return;
        }
        // set_nonblocking(listenfd);
    epoll_event ev{};
    ev.events = EPOLLIN|EPOLLET;
    ev.data.fd = listenfd;
    if(epoll_ctl(epollfd,EPOLL_CTL_ADD,listenfd,&ev)<0){
        perror("add listenfd");
        return;
    }
}

void webserver::dealWithConn(){
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int clientfd = accept(listenfd,(struct sockaddr*)&client_addr,&client_len);
    conn[clientfd] = new http_handler(clientfd,epollfd);
    conn[clientfd]->process();
}

void webserver::dealWithRead(int clientfd){
    conn[clientfd]->process();
}

void webserver::dealWithWrite(int clientfd){
    conn[clientfd]->write();
}

void webserver::eventAccept(){
    while(true){
        int number = epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
        if(number<0 && errno!=EINTR){
            perror("epoll_wait");
            return;
        }
        for(int i=0;i<number;i++){
            int fd = events[i].data.fd;
            if(fd==listenfd){
                dealWithConn();
            }
            else if(events[i].events & EPOLLIN){
                dealWithRead(fd);
            }
            else if(events[i].events & EPOLLOUT){
                dealWithWrite(fd);
            }
        }
    }
}