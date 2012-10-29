#ifndef RTMP_ACTOR_H
#define RTMP_ACTOR_H

#include "rtmpmsg.h"
#include <boost/shared_ptr.hpp>
#include <string>

using namespace std;

class RtmpActor
{
    public:
    virtual ~RtmpActor(){}

    virtual bool onConnect(ConnectCmdPtr cmd) = 0;
    virtual void onDisconnect() = 0;
    virtual bool onPublish(int streamId, string publishUrl) = 0;
    virtual bool onCreateStream(int nextStreamId) = 0;

    virtual bool onMetaData(int streamId, MetaDataMsgPtr metaData) = 0;
    virtual bool onReceiveStream(int streamId, bool isVideo, RtmpMsgHeaderPtr msg) = 0;
};

typedef boost::shared_ptr<RtmpActor> RtmpActorPtr;

#endif
