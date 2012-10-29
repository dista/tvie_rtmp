#ifndef RTMP_PARSER_H
#define RTMP_PARSER_H

#include "rtmpmsg.h"
#include "readbuffer.h"
#include "writebuffer.h"
#include <vector>
#include <utility>

using namespace std;

struct StreamContext
{
    int64_t timestamp;
    int32_t timestampDelta;

    RtmpMsgHeaderPtr prevMsg;

    StreamContext():timestamp(-1), timestampDelta(-1)
    {
    }

    StreamContext(StreamContext& sc)
    {
        timestampDelta = sc.timestampDelta;
        timestamp = sc.timestamp;
        prevMsg = sc.prevMsg;
    }
};

class RtmpParser
{
    private:
        ReadBuffer* rb_;

        vector< pair<int, StreamContext*> > streamContexts_;
        vector< pair<int, StreamContext*> > savedStreamContexts_;
        vector<RtmpMsgHeaderPtr> msgsBuffer_;
        vector<RtmpMsgHeaderPtr> savedMsgsBuffer_;

        RtmpMsgHeaderPtr parseMsgHeaderOnly();
        void parseMsgHeaderPartial(int& chunkSize, RtmpMsgHeaderPtr& prevMh);

        RtmpMsgHeaderPtr parseMsgHeader(int chunkSize, RtmpMsgHeaderPtr* parseMsgHeader);
        void             onMsgHeaderParsed(RtmpMsgHeaderPtr& mh);
        StreamContext* getStreamContext(int streamId);
        void clearStreamContext(vector< pair<int, StreamContext*> >& context);
        void copyStreamContext(vector< pair<int, StreamContext*> >& dst, 
                vector< pair<int, StreamContext*> >& src);

    public:
        RtmpParser(ReadBuffer* rb);
        ~RtmpParser();
        RtmpMsgHeaderPtr parseMsgHeader(int chunkSize);
        ConnectCmdPtr    parseConnectCmd(RtmpMsgHeaderPtr& mh);
        WindowAckSizeMsgPtr parseWindowAckSizeMsg(RtmpMsgHeaderPtr& mh);
        ConnectCmdObjKey CmdConnectIsKeyValid(string& keyName);
        AMF0Commands     peekAMF0Cmd(RtmpMsgHeaderPtr& mh);
        AMF0DataTypes peekAMF0DataType(RtmpMsgHeaderPtr& mh);
        ReleaseStreamCmdPtr parseReleaseStreamCmd(RtmpMsgHeaderPtr& mh);
        FCPublishCmdPtr parseFCPublishCmd(RtmpMsgHeaderPtr& mh);
        CreateStreamCmdPtr parseCreateStreamCmd(RtmpMsgHeaderPtr& mh);
        PublishCmdPtr parsePublishCmd(RtmpMsgHeaderPtr& mh);
        MetaDataMsgPtr parseMetaData(RtmpMsgHeaderPtr& mh);
        void saveContext();
        void restoreContext();
};

typedef vector< pair<int, StreamContext*> >::iterator StreamContextMapIt;

#endif
