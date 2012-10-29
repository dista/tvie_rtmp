#ifndef RTMP_SERVER_H
#define RTMP_SERVER_H

#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include "rtmpconnection.h"
#include "rtmpactor.h"
#include <vector>
#include <boost/date_time.hpp>
#include <boost/thread.hpp>
#include <list>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

using boost::asio::ip::tcp;
using namespace std;

class RtmpConnection;
typedef boost::shared_ptr<RtmpConnection> RtmpConnectionPtr;

typedef boost::shared_ptr< vector<RtmpActorPtr> > RtmpActorsPtr;

typedef RtmpActor* (*createActorFn)();

class RtmpServer
{
    private:
        int listenPort_;
        createActorFn cafn_;
        int serverSock_;
        struct sockaddr_in serverAddr_;
        list< boost::shared_ptr<boost::thread> > clientThreads_;

    public:
        RtmpServer(int listenPort, createActorFn fn);
        ~RtmpServer();
        void start();   

    private:
        void clientCycle(int clientSock, struct sockaddr_in clientAddr);
        void prepare();
        void cleanClientThread();
};


#endif
