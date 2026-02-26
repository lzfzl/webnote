#include "webserver.h"

int main(int argc,char* argv[]){
    std::unique_ptr<webserver> sv(new webserver());
    if(!sv->eventListen()) return 1;
    if(!sv->setTimer()) return 1;
    sv->eventAccept();
}
