#include "workthread.h"


threadpoll::threadpoll(int thread_num){
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
                    cur_hh = task.front();
                    task.pop();
                }
                if(cur_hh->RorW==0){
                    if(!cur_hh->process()){
                        cur_hh->close_conn();
                    };
                }
                else{
                    if(!cur_hh->write()){
                        cur_hh->close_conn();
                    }
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