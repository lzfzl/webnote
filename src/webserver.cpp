#include "webserver.h"
webserver::webserver():tp(&sp,MAX_WORKER_NUMBER),sp("localhost","websudo","webtest","userplan",5){};
webserver::~webserver(){
    close(listenfd);
    close(epollfd);
}
bool webserver::setTimer(){
    m_timerfd = timerfd_create(CLOCK_REALTIME,0);
    if(m_timerfd ==-1){
        perror("timerfd_create");
        return false;
    }
    struct itimerspec new_value;
    new_value.it_value.tv_sec = 1;
    new_value.it_value.tv_nsec = 0;
    new_value.it_interval.tv_sec = 30;
    new_value.it_interval.tv_nsec = 0;

    if(timerfd_settime(m_timerfd,0,&new_value,NULL)==-1){
        perror("timerfd_setting");
        return false;
    }
    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = m_timerfd;
    if(epoll_ctl(epollfd,EPOLL_CTL_ADD,m_timerfd,&ev)<0){
        perror("epoll_ctl timerfd");
        return false;
    }
    return true;
}

bool webserver::eventListen(){
    listenfd = socket(AF_INET,SOCK_STREAM,0);
    if(listenfd<0){
        perror("socket");
        return false;
    }
    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);//able to accept 所有接口上的连接请求,4 bytes
    address.sin_port = htons(8080);//16 bits
    int ret = bind(listenfd,(struct sockaddr *)&address,sizeof(address));
    if(ret<0){
        perror("bind");
        return false;
    }
    ret = listen(listenfd,5);
    if(ret<0){
        perror("listen");
        return false;
    }
    epollfd = epoll_create1(EPOLL_CLOEXEC);
    if(epollfd<0){
        perror("epoll_create");
        return false;
        }
    epoll_event ev{};
    ev.events = EPOLLIN|EPOLLET;
    ev.data.fd = listenfd;
    int old = fcntl(listenfd, F_GETFL, 0);
    fcntl(listenfd, F_SETFL, old | O_NONBLOCK);
    if(epoll_ctl(epollfd,EPOLL_CTL_ADD,listenfd,&ev)<0){
        perror("add listenfd");
        return false;
    }
    return true;
}

void webserver::dealWithConn(){
    while(true){
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int clientfd = accept(listenfd,(struct sockaddr*)&client_addr,&client_len);
        if (clientfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; 
            perror("accept");
            break;
        }
        if(clientfd>=MAX_CONNECT_NUMBER){
            close(clientfd);
        }
        else{
            conn[clientfd].init(clientfd,epollfd,true);
            timedeal.addConn(conn+clientfd);
            tp.addTask(conn+clientfd,0); 
        }
}
}

void webserver::dealWith(int clientfd,int RorW){
    timedeal.adjustConn(conn+clientfd);
    tp.addTask(conn+clientfd,RorW);
    
}

void webserver::eventAccept(){
    while(true){
        int number = epoll_wait(epollfd,events,MAX_EVENT_NUMBER,10);
        if(number<0 && errno!=EINTR){
            perror("epoll_wait");
            return;
        }
        for(int i=0;i<number;i++){
            int fd = events[i].data.fd;
            if(fd==listenfd){
                dealWithConn();
            }
            else if(fd==m_timerfd){
                uint64_t exp;
                ssize_t s = read(m_timerfd, &exp, sizeof(exp));
                timedeal.clear();
            }
            else if(events[i].events & EPOLLIN){
                if(fd!=conn[fd].m_clifd)continue;
                dealWith(fd,0);
            }
            else if(events[i].events & EPOLLOUT){
                if(fd!=conn[fd].m_clifd)continue;
                dealWith(fd,1);
            }
        }
    }
}
