#include "timer.h"
timenode::timenode(http_handler *conn){
    data = conn;
    ddl = std::time(nullptr)+LATENT;
    pre = nullptr;
    next = nullptr;
}
int timenode::getFd(){
    return data->m_clifd;
}
void timenode::remove(){
    int fd = data->m_clifd;
    epoll_ctl(data->m_epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
    data->init(0,0);
}
timer::timer(){
    dummy = new timenode(nullptr);
}
bool timer::addConn(http_handler *conn){
    timenode* nconn = new timenode(conn);
    timenode* i= dummy;
    for (; i->next!=nullptr; i = i->next)
    {
       if(i->next->ddl>=nconn->ddl){
            break;
       }
    }
    nconn->next = i->next;
    nconn->pre = i;
    i->next = nconn;
    return true;
}
bool timer::adjustConn(http_handler *conn){
    timenode* i;
    for (i = dummy->next; i!=nullptr; i = i->next)
    {
        if(i->getFd()==conn->m_clifd)break;
    }
    auto pre = i->pre;
    i->pre = nullptr;
    pre->next = i->next;
    i->next->pre = pre;
    i->next = nullptr;
    i->ddl = std::time(nullptr)+LATENT;
    timenode* j;
    for (j = pre; j->next!=nullptr; j = j->next)
    {
        if(j->next->ddl>=i->ddl){
            break;
       }
    }
    i->next = j->next;
    i->pre = j;
    j->next = i;
    return true;
}
bool timer::removeConn(http_handler* conn){
    timenode* i;
    for (i = dummy->next; i!=nullptr; i = i->next)
    {
        if(i->getFd()==conn->m_clifd)break;
    }
    auto pre = i->pre;
    i->pre = nullptr;
    pre->next = i->next;
    i->next->pre = pre;
    i->next = nullptr;
    i->remove();
    delete i;
    return true;
}
void timer::clear(){
    time_t cur = std::time(nullptr);
    timenode* head;
    for(timenode* i = dummy->next;i!=nullptr;){
        if(i->ddl<=cur){
            head = i->next;
            i->remove();
            delete i;
            i = head;
        }
        else{
            dummy->next = i;
            if(i)i->pre = dummy;
        }
    }   
}

