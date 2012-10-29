#ifndef LIVE_RECEIVER_ACTOR_H
#define LIVE_RECEIVER_ACTOR_H

#include "tviertmp.h"
#include <string>
#include <list>
#include <boost/shared_ptr.hpp>

extern "C"
{
#define __STDC_CONSTANT_MACROS
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

using namespace std;

class StreamSetupInfo
{
public:
    //rtmp stream id
    int streamId;

    int ffVideoIndex;
    int ffAudioIndex;

    int streamInfoFound;
    bool hasVideo;
    bool hasAudio;

    bool isFlvHeaderWritten();
    bool isInputCreated();
    void writeFlvHeader();
    void writeMetaData(MetaDataMsgPtr& meta);
    void writeData(RtmpMsgHeaderPtr& msg);
    int feedData(uint8_t *buf, int buf_size);
    void createInput();
    void setEndOfFile();

    AVFormatContext* getFormatContext();

    StreamSetupInfo(int streamId):
        streamId(streamId),
        ffVideoIndex(-1),
        ffAudioIndex(-1),
        streamInfoFound(false),
        hasVideo(false),
        hasAudio(false),
        flvHeaderWritten_(false),
        rb_(1024),
        wb_(32),
        inCtx_(NULL),
        inputIOBuffer_(NULL),
        mt_(),
        endOfFile_(false),
        error_(false)
    {
    }

    ~StreamSetupInfo();

private:
    const static int INPUT_IO_BUFFER_SIZE = 32768;
    bool flvHeaderWritten_;
    ReadBuffer rb_;
    WriteBuffer wb_;
    AVFormatContext* inCtx_;
    // the buffer will be released by call avformat_close_input
    uint8_t* inputIOBuffer_;
    boost::mutex mt_;
    bool endOfFile_;
    bool error_;
    void writeTagSize(int32_t tagSize);
};

struct RtmpPushProtol
{
    static int read_packet(void *opaque, uint8_t *buf, int buf_size);
};

typedef boost::shared_ptr<AVPacket> AVPacketPtr;

class LiveReceiverActor : public RtmpActor
{
    private:
        const static int STREAM_COUNT = 10;
        static string urlPrefix;
        static string fmt;
        static bool initialized;
        

        AVFormatContext* ctx_;         
        ConnectCmdPtr connectInfo_;
        StreamSetupInfo* streamInfos_[LiveReceiverActor::STREAM_COUNT];
        int streamMap_[LiveReceiverActor::STREAM_COUNT];
        int streamInfoCount_;
        string outputUrl_;
        int avStreamIndex_;
        bool headerWritten_;

        int64_t startTime_;
        boost::thread* th_;
        MetaDataMsgPtr metaData_;
        AVPacket* pkt_;
        boost::mutex mt_;
        bool isPushThreadDone_;

        StreamSetupInfo* findStreamSetupInfo(int streamId);
        bool setOutputCtx(AVFormatContext* inCtx);

        void notifyPushThreadDone();
        bool isPushThreadDone(); 

    public:

        static void Init(string urlPrefix, string fmt);
        LiveReceiverActor();
        ~LiveReceiverActor();

        static RtmpActor* createActor();

        bool onConnect(ConnectCmdPtr cmd);
        void onDisconnect();
        bool onPublish(int streamId, string publishUrl);
        bool onCreateStream(int nextStreamId);

        bool onMetaData(int streamId, MetaDataMsgPtr metaData);
        bool onReceiveStream(int streamId, bool isVideo, RtmpMsgHeaderPtr msg);
        void pushThread(); 
};


#endif
