#include "workthread.h"


threadpoll::threadpoll(sqlPools *sqlpool,int thread_num)
    : m_sqlpool(sqlpool)
{
    m_stop = false;
    if(thread_num<=0)thread_num=1;
    workers.reserve(thread_num);
    for(int i=0;i<thread_num;i++){
        workers.emplace_back([this]{
            while (true)
            {
                http_handler* cur_hh;
                {
                    std::unique_lock<std::mutex> lock_(m_mutex);
                    m_cv.wait(lock_,[this]{return m_stop||!task.empty();});
                    if(m_stop && task.empty()) return;
                    cur_hh = task.front();
                    denug.emplace_back(cur_hh);
                    if(cur_hh->RorW==0 && !cur_hh->sqlconn) cur_hh->sqlconn = m_sqlpool->getaConnect();
                    task.pop();
                }
                if(cur_hh->RorW==0){
                    if(!cur_hh->process()){
                        cur_hh->close_conn();
                    }
                }
                else{
                    if(!cur_hh->write()){
                        cur_hh->close_conn();
                    }
                }
                if(!cur_hh->m_keep_alive && cur_hh->sqlconn){
                    m_sqlpool->releaseConnect(cur_hh->sqlconn);
                    cur_hh->sqlconn = nullptr;
                }

            }
        });
    }
}
void threadpoll::addTask(http_handler* hh,int RorW){
    std::unique_lock<std::mutex> lock_(m_mutex);
    hh->RorW = RorW;
    task.push(hh);
    m_cv.notify_one();
}
threadpoll::~threadpoll(){
    {
        std::unique_lock<std::mutex> lock_(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    for(auto &t: workers){
        if(t.joinable()) t.join();
    }
}
