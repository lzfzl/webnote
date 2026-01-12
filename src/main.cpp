#include "webserver.h"

int main(int argc,char* argv[]){
    webserver sv;
    sv.eventListen();
    sv.eventAccept();
}