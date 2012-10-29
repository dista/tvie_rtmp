#ifndef RTMP_MSG_H
#define RTMP_MSG_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <boost/shared_ptr.hpp>
#include <vector>
#include "rtmpexception.h"

using namespace std;

struct RtmpMsgHeader
{
    uint8_t chunkType;
    int32_t chunkStreamId;
    int64_t timestamp;
    int32_t length;
    uint8_t typeId;
    int32_t streamId;
    uint8_t* body;
    int64_t extendtedTimestamp;

    // contains msg headers not belong to itself
    int32_t unParsedSize;
    boost::shared_ptr<RtmpMsgHeader> prevMsgHeader;

    RtmpMsgHeader():
        chunkType(0), chunkStreamId(-1), timestamp(-1), length(-1),
        typeId(0), streamId(-1), body(NULL), extendtedTimestamp(-1),
        unParsedSize(-1), prevMsgHeader()
    {
    }

    ~RtmpMsgHeader()
    {
        if(body)
            delete[] body;
    }

    boost::shared_ptr<RtmpMsgHeader> findPrev(int32_t chunkStreamId)
    {
        boost::shared_ptr<RtmpMsgHeader> tmp = prevMsgHeader;
        while(tmp)
        {
            // find the last un-full message
            if(tmp->chunkStreamId == chunkStreamId && tmp->unParsedSize > 0)
            {
                break;
            }

            tmp = tmp->prevMsgHeader;
        }
        
        return tmp;
    }

    void appendType3(boost::shared_ptr<RtmpMsgHeader> type3Msg)
    {
        memcpy(body + length - unParsedSize, type3Msg->body, type3Msg->length);
        unParsedSize -= type3Msg->length;
    }

    void appendData(uint8_t* data, int len)
    {
        memcpy(body + length - unParsedSize, data, len);
        unParsedSize -= len;
    }
};

typedef boost::shared_ptr<RtmpMsgHeader> RtmpMsgHeaderPtr;

enum AudioCodecConst
{
    SUPPORT_SND_NONE          = 0x0001,
    SUPPORT_SND_ADPCM         = 0x0002,
    SUPPORT_SND_MP3           = 0x0004,
    SUPPORT_SND_INTEL         = 0x0008,
    SUPPORT_SND_UNUSED        = 0x0010,
    SUPPORT_SND_NELLY8        = 0x0020,
    SUPPORT_SND_NELLY         = 0x0040,
    SUPPORT_SND_G711A         = 0x0080,
    SUPPORT_SND_G711U         = 0x0100,
    SUPPORT_SND_NELLY16       = 0x0200,
    SUPPORT_SND_AAC           = 0x0400,
    SUPPORT_SND_SPEEX         = 0x0800,
    SUPPORT_SND_ALL           = 0x0FFF
};

enum VideoCodecConst
{
    SUPPORT_VID_UNUSED        = 0x0001,
    SUPPORT_VID_JPEG          = 0x0002,
    SUPPORT_VID_SORENSON      = 0x0004,
    SUPPORT_VID_HOMEBREW      = 0x0008,
    SUPPORT_VID_VP6           = 0x0010,
    SUPPORT_VID_VP6ALPHA      = 0x0020,
    SUPPORT_VID_HOMEBREWV     = 0x0040,
    SUPPORT_VID_H264          = 0x0080,
    SUPPORT_VID_ALL           = 0x00FF
};

enum ObjectEncodingConst
{
    kAMF0 = 0,
    kAMF3 = 3,
    OEC_UnKnown = 0xff
};

struct ConnectCmd
{
    double transactionId;
    string app;
    string flashver;
    string swfUrl;
    string tcUrl;
    string type;
    bool fpad;
    AudioCodecConst audioCodecs;
    VideoCodecConst videoCodecs;
    string pageUrl;
    ObjectEncodingConst objectEncoding;

    ConnectCmd():
        transactionId(0), fpad(0), audioCodecs(SUPPORT_SND_ALL), videoCodecs(SUPPORT_VID_ALL),
        objectEncoding(OEC_UnKnown)
    {
    }
};


typedef boost::shared_ptr<ConnectCmd> ConnectCmdPtr;

#define CCK_VALID_KEYS_NUM 10 

enum ConnectCmdObjKey
{
    CCK_App,
    CCK_Flashver,
    CCK_SwfUrl,
    CCK_TcUrl,
    CCK_Type,  // XXX: it is not in rtmp spec, but fmle sends it
    CCK_Fpad,
    CCK_AudioCodecs,
    CCK_VideoCodecs,
    CCK_PageUrl,
    CCK_ObjectEncoding,
    CCK_Unknown = 0xff
};

struct ReleaseStreamCmd
{
    double transactionId;    
    string streamName;
};

typedef boost::shared_ptr<ReleaseStreamCmd> ReleaseStreamCmdPtr;

struct FCPublishCmd
{
    double transactionId;
    string streamName;
};

typedef boost::shared_ptr<FCPublishCmd> FCPublishCmdPtr;

struct CreateStreamCmd
{
    double transactionId;
};

typedef boost::shared_ptr<CreateStreamCmd> CreateStreamCmdPtr;

struct PublishCmd
{
    double transactionId;
    string publishingName;
    string publishingType;
};

typedef boost::shared_ptr<PublishCmd> PublishCmdPtr;

enum RtmpMsgType
{
    MST_SetChunkSize = 1,
    MST_AbortMsg = 2,
    MST_Acknowledgement = 3,
    MST_UserControlMsg = 4,
    MST_WndAckSize = 5,
    MST_SetPeerBandwidth = 6,
    MST_Audio = 8,
    MST_Video = 9,
    MST_DataAMF3 = 15,
    MST_SharedObjAMF3 = 16,
    MST_CmdAMF3 = 17,
    MST_DataAMF0 = 18,
    MST_SharedObjAMF0 = 19,
    MST_CmdAMF0 = 20,
    MST_Aggregate = 22
};

enum UserControlMsgType
{
    UCMT_StreamBegin = 0,
    UCMT_StreamEOF = 1,
    UCMT_StreamDry = 2,
    UCMT_SetBufferLength = 3,
    UCMT_StreamIsRecorded = 4,
    UCMT_PingRequest = 6,
    UCMT_PingResponse = 7
};

enum RtmpLimitType
{
    RLT_Hard = 0,
    RLT_Soft = 1,
    RLT_Dynamic = 2
};

struct WindowAckSizeMsg
{
    int32_t windowAckSize;
};

typedef boost::shared_ptr<WindowAckSizeMsg> WindowAckSizeMsgPtr;

enum AMF0Commands
{
    AMF0_Connect,
    AMF0_ReleaseStream,
    AMF0_FCPublish,
    AMF0_CreateStream,
    AMF0_Publish
};

enum AMF0DataTypes
{
    AMF0_DATA_SetDataFrame
};

enum RtmpMessages
{
    RMS_WindowAckSize,
    RMS_SetPeerBandwidth,
    RMS_SetChunkSize,
    RMS_ReleaseStream,
    RMS_FCPublish,
    RMS_CreateStream,
    RMS_Connect,
    RMS_Publish,
    RMS_OnMetadata,
    RMS_Audio,
    RMS_Video
};

struct MetaDataMsg
{
    string author;
    string copyright;
    string description;
    string keywords;
    string rating;
    string title;
    string presetname;
    string creationdate;
    string videodevice;
    int framerate;
    double width;
    double height;
    string videocodecid;
    double videodatarate;
    int    avclevel;
    int    avcprofile;
    int videokeyframe_frequency;
    string audiodevice;
    double audiosamplerate;
    int audiochannels;
    int audioinputvolume;
    string audiocodecid;
    double audiodatarate;

    //raw data of metadata
    uint8_t* metadata;
    int32_t metadata_size;
    int64_t timestamp;

    MetaDataMsg(): width(-1), audiosamplerate(-1),
    metadata(NULL), metadata_size(0), timestamp(0)
    {
    }

    ~MetaDataMsg()
    {
        if(metadata)
        {
            delete[] metadata;
            metadata = NULL;
            metadata_size = 0;
        }
    }
};

typedef boost::shared_ptr<MetaDataMsg> MetaDataMsgPtr;

#endif
