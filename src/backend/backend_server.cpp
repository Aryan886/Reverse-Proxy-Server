#include "backend_server.h"
using namespace std;

BackendServer::BackendServer(string host, int port) 
    : host_(move(host)), port_(port){}

const string& BackendServer::getHost() const{
    return host_;
}

int BackendServer::getPort() const{
    return port_;
}

int BackendServer::getActiveConnections() const{
    return activeConnections_;
}

void BackendServer::incrementConnections(){
    ++activeConnections_;
}

void BackendServer :: decremenetConnections(){
    if(activeConnections_ >0 ){
        --activeConnections_;
    }
}

bool BackendServer::isHealthy() const{
    return isHealthy_;
}

void BackendServer::setHealthy(bool healthy){
    isHealthy_ = healthy;
}