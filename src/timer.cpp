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
bool timer::clearEmpty(){
    timenode* i = dummy->next; 

    while (i != nullptr) {
        if (i->getFd() == 0) {
            timenode* toDelete = i;
            i->pre->next = i->next;
            if (i->next != nullptr) {
                i->next->pre = i->pre;
            }
            delete toDelete;
        }
        i = i->next;
    }
}

bool timer::addConn(http_handler *conn){
    clearEmpty();
    timenode* nconn = new timenode(conn);
    timenode* i= dummy,*insert = dummy;
    for (;i->next!=nullptr; i = i->next)
    {   
        if(i->next->getFd()==conn->m_clifd){
            insertConn(i->next);
            delete nconn;
            return true;
        }
        if(i->next->ddl>=nconn->ddl){
            insert = i;
       }
    }
    nconn->next = insert->next;
    nconn->pre = insert;
    insert->next = nconn;
    return true;
}

timenode* timer::findConn(http_handler *conn){
    timenode* i;
    for (i = dummy->next; i!=nullptr; i = i->next)
    {
        if(i->getFd()==conn->m_clifd)break;
    }
    return i;
}
bool timer::insertConn(timenode* i){
    auto pre = i->pre;
    i->pre = nullptr;
    pre->next = i->next;
    if(i->next)i->next->pre = pre;
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
    if(j->next)j->next->pre = i;
    i->pre = j;
    j->next = i;
    return true;
}
bool timer::adjustConn(http_handler *conn){
    addConn(conn);
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
            break;
        }
    }   
}



