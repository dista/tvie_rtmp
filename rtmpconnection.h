#ifndef RTMP_CONNECTION_H
#define RTMP_CONNECTION_H

#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/enable_shared_from_this.hpp>
#include "readbuffer.h"
#include "writebuffer.h"
#include "rtmpparser.h"
#include "rtmpmsg.h"
#include "rtmpactor.h"
#include "amf0.h"

#include <sys/types.h>
#include <sys/socket.h>


using namespace boost::asio;
using boost::asio::ip::tcp;

enum RtmpConnectionState
{
    RCS_Uninitialized,
    RCS_HandShake,
    RCS_Normal_Exchange,
    RCS_Closed
};

enum HandshakeState
{
    HSS_Uninitialized,
    HSS_VersionSent,
    HSS_VersionSent_C1_Read,
    HSS_AckSent,
    HSS_HandshakeDone
};

enum NormalExchangeState
{
    NES_NoState,
    NES_CM_connect
};

enum ConnectState
{
    CS_WaitConnect,
    CS_WindowsAckSizeSent,
    CS_SetPeerBandWidthSent,
    CS_SetChunkSizeSent,
    CS_SuccessSent,
    CS_WaitWindowAckSize,
    //CS_StreamBeginSent,
    CS_Done
};

class RtmpServer;

class RtmpConnection
{
    public:
       RtmpConnection(int sockfd, struct sockaddr_in clientAddr, RtmpActorPtr actor);
       virtual ~RtmpConnection();
       void handleClient();

    private:
       const static int RANDOM_DATA_SIZE = 1528;
       const static int READ_BUFFER_INIT_SIZE = 1024;
       const static int WRITE_BUFFER_INIT_SIZE = 1024;
       const static int BUFFER_SIZE = 40960;
       int sockfd_;
       struct sockaddr_in clientAddr_;
       bool isDisconnected_;
       uint8_t buffer_[RtmpConnection::BUFFER_SIZE];
       bool c1_handled;

       // will be update if Set chunk size called
       int chunkSize_;
       int windowAckSize_;

       int outChunkSize_;

       int streamIndex_;

       ReadBuffer rb_;
       WriteBuffer wb_;
       AMF0Serializer amf0s_;

       uint32_t s1Timestamp_;
       uint8_t* s1Randomdata_;
       uint32_t bytesReceived_;
       uint32_t ackBytes_;

       ConnectCmdPtr ccp_;

       // state to parse client request
       RtmpConnectionState rcs_state_;
       HandshakeState hss_state_;
       NormalExchangeState nes_state_;
       ConnectState cs_state_;

       RtmpParser parser_;
       RtmpActorPtr actor_;
       bool isConnected_;

       void disconnect();
       void handshake();
       void handleC0();
       void handleC1();
       void handleC2();
       void handleRead(int bytes_transferred);
       void nextMove();
       void normalExchange();
       void normalExchange(RtmpMsgHeaderPtr& mh);
       void writeData(uint8_t* data, int size, bool del);

       void writeHeader(RtmpMsgHeaderPtr& hd);
       void sentWndAckSize(int size);
       void sentSetPeerBandwidth(int size, RtmpLimitType limitType);
       void sentChunkSize(int chunkSize);
       void sendAcknowledgement(uint32_t sequenceNumber);
       void sentNetConnectConnectSuccess();
       void sentOnBWDone();
       void chunkedSentMsg(RtmpMsgHeaderPtr& mh);

       void onReadReleaseStream(RtmpMsgHeaderPtr& mh);
       void onReadFCPublish(RtmpMsgHeaderPtr& mh);
       void onReadCreateStream(RtmpMsgHeaderPtr& mh);
       void onReadPublish(RtmpMsgHeaderPtr& mh);
       void onReadConnect(RtmpMsgHeaderPtr& mh);
       void onReadWndAckSize(RtmpMsgHeaderPtr& mh);
       void onReadAMF0DataSetDataFrame(RtmpMsgHeaderPtr& mh);
       void onSetChunkSize(RtmpMsgHeaderPtr& mh);
       void onAudio(RtmpMsgHeaderPtr& mh);
       void onVideo(RtmpMsgHeaderPtr& mh);
       
       void sendOnStatus(RtmpMsgHeaderPtr& mh, int transactionId, string code, string description, string clientId);

};

typedef boost::shared_ptr<RtmpConnection> RtmpConnectionPtr;

#endif
