#include "rtmpconnection.h"
#include "log.h"
#include "utility.h"
#include <iostream>
#include <boost/bind.hpp>
#include <string>

using namespace std;

RtmpConnection::RtmpConnection(int sockfd, struct sockaddr_in clientAddr, RtmpActorPtr actor):
    sockfd_(sockfd), clientAddr_(clientAddr), isDisconnected_(false), c1_handled(false), chunkSize_(128), 
    windowAckSize_(-1), outChunkSize_(128), streamIndex_(1), rb_(RtmpConnection::READ_BUFFER_INIT_SIZE),
    wb_(WRITE_BUFFER_INIT_SIZE), 
    amf0s_(&wb_),
    s1Timestamp_(0),
    s1Randomdata_(NULL),
    bytesReceived_(0),
    ackBytes_(0),
    rcs_state_(RCS_Uninitialized),
    hss_state_(HSS_Uninitialized),
    nes_state_(NES_NoState),
    cs_state_(CS_WaitConnect),
    parser_(&rb_), actor_(actor),
    isConnected_(false) 
{
    // the default chunk size is 128
}

RtmpConnection::~RtmpConnection()
{
    if(s1Randomdata_)
    {
        delete[] s1Randomdata_;
    }

    if(sockfd_ != -1)
    {
        close(sockfd_);
        sockfd_ = -1;
    }
}

void RtmpConnection::handleClient()
{
    int bytesReceived;

    while(true)
    {
        bytesReceived = recv(sockfd_, buffer_, RtmpConnection::BUFFER_SIZE, 0);

        // Error or client close
        if(bytesReceived <= 0)
        {
            //RTMP_LOG("ASIO read error, %d\n", error);
            disconnect();
            return;
        }

        handleRead(bytesReceived); 
    }
}

void RtmpConnection::handleRead(int bytes_transferred)
{
    RTMP_LOG(LEVDEBUG, "RtmpConnection::handleRead, bytes_transferred: %d\n", bytes_transferred); 

    bytesReceived_ += bytes_transferred;

    if(bytesReceived_ < ackBytes_)
    {
        bytesReceived_ += 0xffffffff - (int64_t)ackBytes_;
        ackBytes_ = 0;
    }

    if(windowAckSize_ != -1 && (bytesReceived_ - ackBytes_) >= windowAckSize_ / 2)
    {
        sendAcknowledgement(bytesReceived_);
        ackBytes_ += windowAckSize_ / 2;
    } 

    rb_.appendData(buffer_, bytes_transferred);

    try{
        while(rb_.getUnReadSize() > 0)
        {
            rb_.snapStart();
            parser_.saveContext();

            nextMove();

            rb_.snapClear();
        }
    }
    catch(RtmpBadProtocalData& e)
    {
        RTMP_LOG(LEVERROR, "Decode rtmp protocol error: %s\n", e.what());
        disconnect();
        return;
    }
    catch(RtmpNotSupported& se)
    {
        RTMP_LOG(LEVDEBUG, "Not supported: %s\n", se.what());
        disconnect();
        return;
    }
    catch(RtmpBadState& bse)
    {
        RTMP_LOG(LEVERROR, "Bad state: %s\n", bse.what());
        disconnect();
        return;
    }
    catch(RtmpInternalError& re)
    {
        RTMP_LOG(LEVERROR, "Internal Error: %s\n", re.what());
        disconnect();
        return;
    }
    catch(RtmpNoEnoughData& ne)
    {
        RTMP_LOG(LEVDEBUG, "No enough data\n");
        // no enough data can be caused by buffer limit
        rb_.snapStop();
        parser_.restoreContext(); 
    }
}

void RtmpConnection::nextMove()
{
    switch(rcs_state_)
    {
        case RCS_Uninitialized:
        case RCS_HandShake:
            handshake();
            break;
        case RCS_Normal_Exchange:
            normalExchange();
            break;
        case RCS_Closed:
            return;
    }
}

void RtmpConnection::normalExchange()
{
    RtmpMsgHeaderPtr mh = parser_.parseMsgHeader(chunkSize_);

    normalExchange(mh);
}

void RtmpConnection::handshake()
{
    rcs_state_ = RCS_HandShake;

    switch(hss_state_)
    {
        case HSS_Uninitialized:
            handleC0();
            break;
        case HSS_VersionSent:
            handleC1();
            break;
        case HSS_AckSent:
            handleC2();
            break;
        case HSS_HandshakeDone:
            //never get here
            break;
    }
}

void RtmpConnection::handleC0()
{
    RTMP_LOG(LEVDEBUG, "RtmpConnection::handleC0\n");
    if(rb_.getUnReadSize() < 1)
    {    
        throw RtmpBadProtocalData("expect version data");
    }

    uint8_t version = rb_.readByte();

    if(version != 3)
    {
        throw RtmpBadProtocalData("bad version, not 3");
    }

    wb_.reInit();
    wb_.writeByte(version);
    //S0 sent
    writeData(wb_.getBuffer(), wb_.getBufferCount(), true);

    wb_.reInit();
    s1Timestamp_ = 0;
    wb_.writeB(s1Timestamp_); // time
    wb_.writeB((uint32_t)0); // zero

    s1Randomdata_ = new uint8_t[RtmpConnection::RANDOM_DATA_SIZE];

    //S1 sent
    wb_.writeBytes(s1Randomdata_, RtmpConnection::RANDOM_DATA_SIZE);
    writeData(wb_.getBuffer(), wb_.getBufferCount(), true);
    hss_state_ = HSS_VersionSent;

    RTMP_LOG(LEVDEBUG, "Handshake S1 sent\n");

    // after that, try handleC1
    if(rb_.getUnReadSize() > 4 + 4 + RtmpConnection::RANDOM_DATA_SIZE)
    {
        handleC1();
        hss_state_ = HSS_VersionSent_C1_Read;
    }
}

void RtmpConnection::handleC1()
{
    RTMP_LOG(LEVDEBUG, "RtmpConnection::handleC1\n");
    uint32_t timestamp = rb_.read<uint32_t>(ReadBuffer::BIG); 
    rb_.read<uint32_t>(ReadBuffer::BIG); // ZERO
    uint8_t* randomData = rb_.readBytes(RtmpConnection::RANDOM_DATA_SIZE);

    wb_.reInit();
    wb_.writeB(timestamp);
    wb_.writeB(Utility::getTimestamp());
    wb_.writeBytes(randomData, RtmpConnection::RANDOM_DATA_SIZE);
    
    // S2 sent
    writeData(wb_.getBuffer(), wb_.getBufferCount(), true);

    hss_state_ = HSS_AckSent;

    RTMP_LOG(LEVDEBUG, "Handshake S2 sent\n");

    delete[] randomData;

    // after that, try handleC1
    if(rb_.getUnReadSize() > 4 + 4 + RtmpConnection::RANDOM_DATA_SIZE)
    {
        handleC2();
    }
}

void RtmpConnection::onReadConnect(RtmpMsgHeaderPtr& mh)
{
    RTMP_LOG(LEVDEBUG, "RtmpConnection::onReadConnect\n");
    if(isConnected_)
    {
        throw RtmpBadState("is already connected!");
    }

    ccp_ = parser_.parseConnectCmd(mh);
    if(actor_)
    {
        if(!actor_->onConnect(ccp_))
        {
            throw RtmpInternalError("actor do not allow to connect");
        }
    }

    sentWndAckSize(2500000);
    sentSetPeerBandwidth(2500000, RLT_Dynamic);
    outChunkSize_ = 1024;
    sentChunkSize(outChunkSize_);
    sentNetConnectConnectSuccess();

    isConnected_ = true;

    actor_->onConnect(ccp_);
}

void RtmpConnection::onReadWndAckSize(RtmpMsgHeaderPtr& mh)
{
    RTMP_LOG(LEVDEBUG, "RtmpConnection::onReadWndAckSize\n");
    WindowAckSizeMsgPtr amp = parser_.parseWindowAckSizeMsg(mh);
    windowAckSize_ = amp->windowAckSize;

    sentOnBWDone();
}

void RtmpConnection::normalExchange(RtmpMsgHeaderPtr& mh)
{
    if(mh->typeId == MST_CmdAMF0)
    {
        AMF0Commands cmd = parser_.peekAMF0Cmd(mh); 
        switch(cmd)
        {
            case AMF0_Connect:
                onReadConnect(mh);
                break;
            case AMF0_Publish:
                onReadPublish(mh);
                break;
            case AMF0_ReleaseStream:
                onReadReleaseStream(mh);
                break;
            case AMF0_FCPublish:
                onReadFCPublish(mh);
                break;
            case AMF0_CreateStream:
                onReadCreateStream(mh);
                break;
            default:
                RTMP_LOG(LEVDEBUG, "AMF0 CMD[%d] is not handled\n", cmd);
        }
    }
    else if(mh->typeId == MST_CmdAMF3)
    {
        throw RtmpInternalError("AMF3 command is not supported");
    }
    else if(mh->typeId == MST_DataAMF0)
    {
        try
        {
            AMF0DataTypes dataType = parser_.peekAMF0DataType(mh);
            switch(dataType)
            {
                case AMF0_DATA_SetDataFrame:
                    onReadAMF0DataSetDataFrame(mh);
                    break;
                default:
                    RTMP_LOG(LEVDEBUG, "AMF0 DATA[%d] is not handled\n", dataType);
                    break;
            }
        }
        catch(RtmpNotSupported& e)
        {
            RTMP_LOG(LEVDEBUG, "Encounter unsupported AMF0 DATA: %s\n", e.what());
        }
    }
    else if(mh->typeId == MST_SetChunkSize)
    {
        onSetChunkSize(mh);
    }
    else if(mh->typeId == MST_Audio)
    {
        onAudio(mh);
    }
    else if(mh->typeId == MST_Video)
    {
        onVideo(mh);
    }
    else if(mh->typeId = MST_WndAckSize)
    {
        onReadWndAckSize(mh);
    }
    else if(mh->typeId == MST_UserControlMsg)
    {
        throw RtmpNotImplemented("I think client should not send this");
    }
    else
    {
        RTMP_LOG(LEVDEBUG, "Msg[type: %d] is not handled\n", mh->typeId);
    }
}

void RtmpConnection::onAudio(RtmpMsgHeaderPtr& mh)
{
    RTMP_LOG(LEVDEBUG, "RtmpConnection::onAudio, timestamp %ld\n", mh->timestamp);
    if(!actor_->onReceiveStream(mh->streamId, false, mh))
    {
        throw RtmpInternalError("error on audio");
    }
}

void RtmpConnection::onVideo(RtmpMsgHeaderPtr& mh)
{
    RTMP_LOG(LEVDEBUG, "RtmpConnection::onVideo, timestamp %ld\n", mh->timestamp);
    if(!actor_->onReceiveStream(mh->streamId, true, mh))
    {
        throw RtmpInternalError("error on audio");
    }
}

void RtmpConnection::onSetChunkSize(RtmpMsgHeaderPtr& mh)
{
    ReadBufferPtr readBuffer(new ReadBuffer(mh->length));
    readBuffer->appendData(mh->body, mh->length);

    chunkSize_ = readBuffer->read<int32_t>(ReadBuffer::BIG); 
}

void RtmpConnection::onReadAMF0DataSetDataFrame(RtmpMsgHeaderPtr& mh)
{
    RTMP_LOG(LEVDEBUG, "RtmpConnection::onReadAMF0DataSetDataFrame\n");
    MetaDataMsgPtr request = parser_.parseMetaData(mh);
    request->timestamp = mh->timestamp;

    if(!actor_->onMetaData(mh->streamId, request))
    {
        throw RtmpInternalError("error on metadata");
    }
}

void RtmpConnection::onReadPublish(RtmpMsgHeaderPtr& mh)
{
    RTMP_LOG(LEVDEBUG, "RtmpConnection::onReadPublish\n");
    PublishCmdPtr request = parser_.parsePublishCmd(mh);

    // TODO: do some logic

    std::string name = request->publishingName; 
    int questionMarkPos = request->publishingName.find('?');
    if(questionMarkPos != -1)
    {
        name = request->publishingName.substr(0, questionMarkPos);
    } 

    if(!actor_->onPublish(mh->streamId, name))
    {
        throw RtmpInternalError("error on publish");
    }
    
    sendOnStatus(mh, request->transactionId, "NetStream.Publish.Start", name + " is now published", "oAAQAAAA");
}

void RtmpConnection::sendOnStatus(RtmpMsgHeaderPtr& mh, int transactionId, string code, string description, string clientId)
{
    RTMP_LOG(LEVDEBUG, "RtmpConnection::sendOnStatus\n");
    WriteBufferPtr wp(new WriteBuffer(chunkSize_));
    AMF0SerializerPtr ap(new AMF0Serializer(wp.get()));

    ap->writeString("onStatus");
    ap->writeNumber(transactionId);
    ap->writeNull();

    ap->writeObjectStart();
    ap->writeObjectKey("level");
    ap->writeString("status");
    ap->writeObjectKey("code");
    ap->writeString(code);
    ap->writeObjectKey("description");
    ap->writeString(description);
    ap->writeObjectKey("clientid");
    ap->writeString(clientId);
    ap->writeObjectEnd();

    RtmpMsgHeaderPtr hd(new RtmpMsgHeader());
    hd->body  = wp->getBuffer();
    hd->chunkType = 0;
    hd->chunkStreamId = mh->chunkStreamId;
    hd->timestamp = 0;
    hd->length = wp->getBufferCount();
    hd->typeId = MST_CmdAMF0;
    hd->streamId = mh->streamId;

    chunkedSentMsg(hd);
} 

void RtmpConnection::onReadReleaseStream(RtmpMsgHeaderPtr& mh)
{
    RTMP_LOG(LEVDEBUG, "RtmpConnection::onReadReleaseStream\n");
    ReleaseStreamCmdPtr request = parser_.parseReleaseStreamCmd(mh);

    // TODO: Do some logic..
    
    WriteBufferPtr wp(new WriteBuffer(chunkSize_));
    AMF0SerializerPtr ap(new AMF0Serializer(wp.get()));

    ap->writeString("_result");
    ap->writeNumber(request->transactionId);
    ap->writeNull();
    ap->writeUndefined();

    RtmpMsgHeaderPtr hd(new RtmpMsgHeader());
    hd->body  = wp->getBuffer();
    hd->chunkType = 0;
    hd->chunkStreamId = 3;
    hd->timestamp = 0;
    hd->length = wp->getBufferCount();
    hd->typeId = MST_CmdAMF0;
    hd->streamId = 0;

    chunkedSentMsg(hd);
}

void RtmpConnection::onReadFCPublish(RtmpMsgHeaderPtr& mh)
{
    RTMP_LOG(LEVDEBUG, "RtmpConnection::onReadFCPublish\n");
    FCPublishCmdPtr request = parser_.parseFCPublishCmd(mh);

    WriteBufferPtr wp(new WriteBuffer(chunkSize_));
    AMF0SerializerPtr ap(new AMF0Serializer(wp.get()));

    ap->writeString("_result");
    ap->writeNumber(request->transactionId);
    ap->writeNull();
    ap->writeUndefined();

    RtmpMsgHeaderPtr hd(new RtmpMsgHeader());
    hd->body  = wp->getBuffer();
    hd->chunkType = 0;
    hd->chunkStreamId = 3;
    hd->timestamp = 0;
    hd->length = wp->getBufferCount();
    hd->typeId = MST_CmdAMF0;
    hd->streamId = 0;

    chunkedSentMsg(hd);
}

void RtmpConnection::onReadCreateStream(RtmpMsgHeaderPtr& mh)
{
    RTMP_LOG(LEVDEBUG, "RtmpConnection::onReadCreateStream\n");
    CreateStreamCmdPtr request = parser_.parseCreateStreamCmd(mh);

    if(!actor_->onCreateStream(streamIndex_))
    {
        throw RtmpInternalError("actor onCreateStream failed");
    }

    WriteBufferPtr wp(new WriteBuffer(chunkSize_));
    AMF0SerializerPtr ap(new AMF0Serializer(wp.get()));

    ap->writeString("_result");
    ap->writeNumber(request->transactionId);
    ap->writeNull();
    ap->writeNumber((double)streamIndex_);

    RtmpMsgHeaderPtr hd(new RtmpMsgHeader());
    hd->body  = wp->getBuffer();
    hd->chunkType = 0;
    hd->chunkStreamId = 3;
    hd->timestamp = 0;
    hd->length = wp->getBufferCount();
    hd->typeId = MST_CmdAMF0;
    hd->streamId = 0;

    chunkedSentMsg(hd);

    streamIndex_++;
}

void RtmpConnection::sentOnBWDone()
{
    RTMP_LOG(LEVDEBUG, "RtmpConnection::sentOnBWDone\n");
    WriteBufferPtr wp(new WriteBuffer(chunkSize_));
    AMF0SerializerPtr ap(new AMF0Serializer(wp.get()));

    ap->writeString("onBWDone");
    ap->writeNumber(0);
    ap->writeNull();

    RtmpMsgHeaderPtr hd(new RtmpMsgHeader());
    hd->body  = wp->getBuffer();
    hd->chunkType = 0;
    hd->chunkStreamId = 3;
    hd->timestamp = 0;
    hd->length = wp->getBufferCount();
    hd->typeId = MST_CmdAMF0;
    hd->streamId = 0;

    chunkedSentMsg(hd);
    cs_state_ = CS_Done;
}

void RtmpConnection::sentNetConnectConnectSuccess()
{
    RTMP_LOG(LEVDEBUG, "RtmpConnection::sentNetConnectConnectSuccess\n");
    WriteBufferPtr wp(new WriteBuffer(chunkSize_));
    AMF0SerializerPtr ap(new AMF0Serializer(wp.get()));

    ap->writeString("_result");
    ap->writeNumber(1);

    ap->writeObjectStart();
    ap->writeObjectKey("fmsVer");
    ap->writeString("TVie Rtmp/1,0,0,0");
    ap->writeObjectKey("capalilities");
    ap->writeNumber(255);
    ap->writeObjectKey("mode");
    ap->writeNumber(1);
    ap->writeObjectEnd();

    ap->writeObjectStart();
    ap->writeObjectKey("level");
    ap->writeString("status");
    ap->writeObjectKey("code");
    ap->writeString("NetConnection.Connect.Success");
    ap->writeObjectKey("description");
    ap->writeString("connection succeeded");
    ap->writeObjectKey("objectEncoding");
    ap->writeNumber(0);
    ap->writeObjectEnd();

    RtmpMsgHeaderPtr hd(new RtmpMsgHeader());
    hd->body  = wp->getBuffer();
    hd->chunkType = 0;
    hd->chunkStreamId = 3;
    hd->timestamp = 0;
    hd->length = wp->getBufferCount();
    hd->typeId = MST_CmdAMF0;
    hd->streamId = 0;

    chunkedSentMsg(hd);
    cs_state_ = CS_SuccessSent;
}

void RtmpConnection::chunkedSentMsg(RtmpMsgHeaderPtr& mh)
{
    wb_.reInit();
    writeHeader(mh);

    int bytesLeft = mh->length;
    while(bytesLeft)
    {
        int chunkSize = (outChunkSize_ > bytesLeft) ? bytesLeft : outChunkSize_;

        wb_.writeBytes(mh->body + mh->length - bytesLeft, chunkSize);
        
        bytesLeft -= chunkSize;
        if(bytesLeft > 0)
        {
            wb_.writeByte(0xc3); // type 3 msg
        }
    }

    writeData(wb_.getBuffer(), wb_.getBufferCount(), true);
}

void RtmpConnection::sentChunkSize(int chunkSize)
{
    RTMP_LOG(LEVDEBUG, "RtmpConnection::sentChunkSize, chunkSize %d\n", chunkSize);
    RtmpMsgHeaderPtr hd(new RtmpMsgHeader());
    hd->chunkType = 0;
    hd->chunkStreamId = 2;
    hd->timestamp = 0;
    hd->length = 4;
    hd->typeId = MST_SetChunkSize;
    hd->streamId = 0;
    
    wb_.reInit();
    writeHeader(hd);
    wb_.writeB(chunkSize);

    writeData(wb_.getBuffer(), wb_.getBufferCount(), true);

    cs_state_ = CS_SetChunkSizeSent;
}

void RtmpConnection::sendAcknowledgement(uint32_t sequenceNumber)
{
    RTMP_LOG(LEVDEBUG, "RtmpConnection::sendAcknowledgement, sequenceNumber: %u\n", sequenceNumber);
    RtmpMsgHeaderPtr hd(new RtmpMsgHeader());
    hd->chunkType = 0;
    hd->chunkStreamId = 2;
    hd->timestamp = 0;
    hd->length = 4;
    hd->typeId = MST_Acknowledgement;
    hd->streamId = 0;
    
    wb_.reInit();
    writeHeader(hd);
    wb_.writeB(sequenceNumber);

    writeData(wb_.getBuffer(), wb_.getBufferCount(), true);
}

void RtmpConnection::sentWndAckSize(int size)
{
    RTMP_LOG(LEVDEBUG, "RtmpConnection::sentWndAckSize, size %d\n", size);
    RtmpMsgHeaderPtr hd(new RtmpMsgHeader());
    hd->chunkType = 0;
    hd->chunkStreamId = 2;
    hd->timestamp = 0;
    hd->length = 4;
    hd->typeId = MST_WndAckSize;
    hd->streamId = 0;

    wb_.reInit();
    writeHeader(hd);
    wb_.writeB(size);

    writeData(wb_.getBuffer(), wb_.getBufferCount(), true);

    cs_state_ = CS_WindowsAckSizeSent;
}

void RtmpConnection::sentSetPeerBandwidth(int size, RtmpLimitType limitType)
{
    RTMP_LOG(LEVDEBUG, "RtmpConnection::sentSetPeerBandwidth, size %d, limitType %d\n", size, limitType);
    RtmpMsgHeaderPtr hd(new RtmpMsgHeader()); 
    hd->chunkType = 0;
    hd->chunkStreamId = 2;
    hd->timestamp = 0;
    hd->length = 5;
    hd->typeId = MST_SetPeerBandwidth;
    hd->streamId = 0;

    wb_.reInit();
    writeHeader(hd);
    wb_.writeB(size);
    wb_.writeB((uint8_t)limitType);

    writeData(wb_.getBuffer(), wb_.getBufferCount(), true);
    cs_state_ = CS_SetPeerBandWidthSent;
}

void RtmpConnection::writeHeader(RtmpMsgHeaderPtr& hd)
{
    wb_.writeB(hd->chunkType, 2);
    if(hd->chunkStreamId < 64)
    {
        wb_.writeB(hd->chunkStreamId, 6);
    }
    else if(hd->chunkStreamId >=64 && hd->chunkStreamId <= 319)
    {
        wb_.writeB(0, 6);
        wb_.writeB(hd->chunkStreamId - 64, 8);
    }
    else if(hd->chunkStreamId > 319 && hd->chunkStreamId <= 65599)
    {
        wb_.writeB(1, 6);
        wb_.writeL(hd->chunkStreamId - 64, 16);
    }
    else
    {
        throw RtmpInternalError("chunkStreamId is not correct");  
    }

    if(hd->timestamp != -1)
    {
        if(hd->timestamp < 0x00ffffff)
        {
            wb_.writeB(hd->timestamp, 24);
        }
    }

    wb_.writeB(hd->length, 24);
    wb_.writeB(hd->typeId, 8);

    if(hd->streamId != -1)
    {
        wb_.writeL(hd->streamId, 32);
    }

    if(hd->timestamp != -1 && hd->timestamp > 0x00ffffff)
    {
        wb_.writeB(hd->timestamp - 0x00ffffff, 32); 
    }
}

void RtmpConnection::handleC2()
{
    RTMP_LOG(LEVDEBUG, "Handshake handle C2");
    uint32_t timestamp = rb_.read<uint32_t>(ReadBuffer::BIG);

    /* TODO: this also may be not match
    if(timestamp != s1Timestamp_)
    {
        throw RtmpBadProtocalData("C2 timestamp is not match");
    }
    */

    rb_.skip(4); // time2
    uint8_t* randomData = rb_.readBytes(RtmpConnection::RANDOM_DATA_SIZE);
    /* TODO: should we verify that? spec said we should, but in real world...
    if(!Utility::compareData(s1Randomdata_, randomData, RtmpConnection::RANDOM_DATA_SIZE))
    {
        delete[] randomData;
        throw RtmpBadProtocalData("C2 random data is not match");
    }
    */
    
    delete[] randomData;

    RTMP_LOG(LEVDEBUG, "Handshake done\n");
    hss_state_ = HSS_HandshakeDone;
    rcs_state_ = RCS_Normal_Exchange;
}

void RtmpConnection::writeData(uint8_t* data, int size, bool delData)
{
    int sendSize = send(sockfd_, data, size, 0);

    if(delData)
    {
        delete[] data;
    }
            
    if(sendSize == -1)
    {
        throw RtmpInternalError("send data failed");
    }
}

void RtmpConnection::disconnect()
{
    if(isDisconnected_)
    {
        return;
    }

    if(actor_)
    {
        actor_->onDisconnect();
    }
    
    if(sockfd_ != -1)
    {
        close(sockfd_);
        sockfd_ = -1;
    }

    isDisconnected_ = true;
}
