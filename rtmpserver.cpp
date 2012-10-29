#include "rtmpserver.h"
#include "rtmpconnection.h"
#include "log.h"
#include "boost/make_shared.hpp"

RtmpServer::RtmpServer(int listenPort, createActorFn fn):
    listenPort_(listenPort),
    cafn_(fn),
    serverSock_(-1),
    clientThreads_()    
{
}

RtmpServer::~RtmpServer()
{
    if(serverSock_ != -1)
    {
        close(serverSock_);
        serverSock_ = -1;
    }
}

void RtmpServer::prepare()
{
    if((serverSock_ = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        throw RtmpInternalError("create socket failed");
    }

    int reuseSock = 1;

    if(setsockopt(serverSock_, SOL_SOCKET, SO_REUSEADDR, &reuseSock, sizeof(int)) == -1)
    {
        throw RtmpInternalError("set socket option failed");
    }

    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(listenPort_);
    serverAddr_.sin_addr.s_addr = INADDR_ANY;
    bzero(&(serverAddr_.sin_zero), 8);

    if(::bind(serverSock_, (struct sockaddr *)&serverAddr_, sizeof(struct sockaddr)) == -1)
    {
        throw RtmpInternalError("bind failed");
    }

    if(listen(serverSock_, 10) == -1)
    {
        throw RtmpInternalError("listen failed");
    }

    RTMP_LOG(LEVINFO, "Server listen on port %d\n", listenPort_);
}

void RtmpServer::cleanClientThread()
{
    boost::posix_time::time_duration timeout = boost::posix_time::milliseconds(0);
    list< boost::shared_ptr<boost::thread> >::iterator it = clientThreads_.begin();

    for(; it != clientThreads_.end();)
    {
        // the thread is finished
        if((*it)->timed_join(timeout))
        {
            it = clientThreads_.erase(it);
        }
        else
        {
            it++;
        }
    }
}

void RtmpServer::clientCycle(int clientSock, struct sockaddr_in clientAddr)
{
    RTMP_LOG(LEVINFO, "New client connected, address is %s:%d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
    RtmpConnection rc(clientSock, clientAddr, RtmpActorPtr(cafn_())); 

    rc.handleClient(); 
}

void RtmpServer::start()
{
    prepare();

    socklen_t clientAddrLen = sizeof(struct sockaddr_in);
    struct sockaddr_in clientAddr;
    int clientSock;

    while(true)
    {
        if((clientSock = accept(serverSock_, (struct sockaddr *)&clientAddr, &clientAddrLen))
           == -1)
        {
            throw RtmpInternalError("accept client failed", errno);
        }

        cleanClientThread();

        clientThreads_.push_back(
                boost::make_shared<boost::thread>(boost::bind(&RtmpServer::clientCycle, this, clientSock, clientAddr))
                );
    }
}

