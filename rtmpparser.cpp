#include "rtmpparser.h"
#include <boost/shared_ptr.hpp>
#include <boost/algorithm/string.hpp>
#include "amf0.h"
#include "log.h"
#include "utility.h"

RtmpParser::RtmpParser(ReadBuffer* rb): rb_(rb), 
    streamContexts_(), savedStreamContexts_(), 
    msgsBuffer_(), savedMsgsBuffer_()
{
}

RtmpParser::~RtmpParser()
{
    clearStreamContext(streamContexts_);
    clearStreamContext(savedStreamContexts_);
}

void RtmpParser::clearStreamContext(vector< pair<int, StreamContext*> >& context)
{
     vector< pair<int, StreamContext*> >::iterator it = context.begin();

     for(; it < context.end(); it++)
     {
         delete it->second;
         it->second = NULL;
     }

     context.clear();
}

void RtmpParser::saveContext()
{
    clearStreamContext(savedStreamContexts_);
    copyStreamContext(savedStreamContexts_, streamContexts_);

    savedMsgsBuffer_.clear();

    vector<RtmpMsgHeaderPtr>::iterator it = msgsBuffer_.begin();
    for(; it < msgsBuffer_.end(); it++)
    {
        savedMsgsBuffer_.push_back(*it);
    } 
}

void RtmpParser::restoreContext()
{
    clearStreamContext(streamContexts_);
    copyStreamContext(streamContexts_, savedStreamContexts_);

    msgsBuffer_.clear();

    vector<RtmpMsgHeaderPtr>::iterator it = savedMsgsBuffer_.begin();
    for(; it < savedMsgsBuffer_.end(); it++)
    {
        msgsBuffer_.push_back(*it);
    } 
}

void RtmpParser::copyStreamContext(vector< pair<int, StreamContext*> >& dst, 
        vector< pair<int, StreamContext*> >& src)
{
     vector< pair<int, StreamContext*> >::iterator it = src.begin();

     for(; it < src.end(); it++)
     {
         int streamId = it->first;
         StreamContext* srcCtx = it->second;
         
         pair<int, StreamContext*> p(streamId, new StreamContext(*srcCtx));

         dst.push_back(p);
     }
}

RtmpMsgHeaderPtr RtmpParser::parseMsgHeaderOnly()
{
    RtmpMsgHeaderPtr mh(new RtmpMsgHeader());

    //Utility::dumpData(rb_->getUnReadBufferNoCopy(),
    //            rb_->getUnReadSize(), "/tmp/xg");

    uint8_t firstByte = rb_->readByte();

    mh->chunkType = firstByte >> 6;
    mh->chunkStreamId = firstByte & ((1 << 6) - 1);

    if(mh->chunkType != 0 && mh->chunkType != 1 && mh->chunkType != 2 && mh->chunkType != 3)
    {
        throw RtmpBadProtocalData("chunkType should be 0, 1, 2 or 3");
    }

    if(mh->chunkStreamId == 0)
    {
        mh->chunkStreamId = rb_->readByte() + 64;
    } 
    else if(mh->chunkStreamId == 1)
    {
        // third * 256 + second + 64
        mh->chunkStreamId = rb_->read<uint16_t>(ReadBuffer::LITTLE) + 64;
    }

    StreamContext* sc = NULL;

    switch(mh->chunkType)
    {
        case 0:
            mh->timestamp = rb_->read<uint32_t>(ReadBuffer::BIG, 3);
            mh->length = rb_->read<uint32_t>(ReadBuffer::BIG, 3);
            mh->typeId = rb_->readByte();
            mh->streamId = rb_->read<uint32_t>(ReadBuffer::LITTLE);
            break;
        case 1:
            mh->timestamp = rb_->read<uint32_t>(ReadBuffer::BIG, 3);
            mh->length = rb_->read<uint32_t>(ReadBuffer::BIG, 3);
            mh->typeId = rb_->readByte();
            break;
        case 2:
            mh->timestamp = rb_->read<uint32_t>(ReadBuffer::BIG, 3);
            break;
        case 3:
            sc = getStreamContext(mh->chunkStreamId);
            if(sc != NULL)
            {
                if(sc->prevMsg->extendtedTimestamp != -1 &&
                   sc->prevMsg->extendtedTimestamp == rb_->peek<uint32_t>(ReadBuffer::BIG))
                {
                    // RTMP spec says: (Extended Timestamp, Type 3 chunks MUST NOT have this field)
                    // BUT FMLE sends this!!
                    //

                    mh->timestamp = 0x00ffffff;
                }
            }

            // no header
            break;
        default:
            throw RtmpBadProtocalData("chunk type should be 0, 1, 2, or 3"); 
    }

    if(mh->timestamp == 0x00ffffff)
    {
        mh->extendtedTimestamp = rb_->read<uint32_t>(ReadBuffer::BIG); 
        mh->timestamp = (uint64_t)0x00ffffff + mh->extendtedTimestamp;
    }

    return mh;
}

void RtmpParser::parseMsgHeaderPartial(int& chunkSize, RtmpMsgHeaderPtr& prevMh)
{
    RTMP_LOG(LEVDEBUG, "RtmpParser::parseMsgHeaderPartial\n");
    RtmpMsgHeaderPtr mh = parseMsgHeaderOnly();

    bool isIndependPkt = false;

    if(mh->chunkType == 3)
    {
        do{
            RtmpMsgHeaderPtr tmp;
            if(prevMh->chunkStreamId == mh->chunkStreamId)
            {
                tmp = prevMh;
            }
            else
            {
                tmp = prevMh->findPrev(mh->chunkStreamId);

                if(!tmp){
                    // other type 3 not part of one msg can be found here
                    // we need to handle that!!
                    isIndependPkt = true;
                    break;
                }
            }
            
            mh->length = (chunkSize > tmp->unParsedSize) ? tmp->unParsedSize : chunkSize;
            mh->body = rb_->readBytes(mh->length);

            tmp->appendType3(mh);
            return;
        }while(0);
    }

    if(mh->chunkType != 3 || isIndependPkt)
    {
        onMsgHeaderParsed(mh);
        mh->prevMsgHeader = prevMh;

        RtmpMsgHeaderPtr anotherMsg = parseMsgHeader(chunkSize, &mh);

        // set chunk size impact chunksize immediately
        if(anotherMsg->typeId == 1)
        {
            chunkSize = ReadBuffer::read<int32_t>(anotherMsg->body, anotherMsg->length, ReadBuffer::BIG);
        }

        msgsBuffer_.push_back(anotherMsg);
        return;
    }
}

StreamContext* RtmpParser::getStreamContext(int streamId)
{
    StreamContext* sc = NULL;
    StreamContextMapIt it = streamContexts_.begin();

    for(; it < streamContexts_.end(); it++)
    {
        if(it->first == streamId)
        {
            sc = it->second;
            break;
        } 
    }

    return sc;
}

void RtmpParser::onMsgHeaderParsed(RtmpMsgHeaderPtr& mh)
{
    if(mh->typeId == MST_Audio || mh->typeId == MST_Video || mh->chunkType == 2 || mh->chunkType == 3 || mh->typeId == MST_DataAMF0)
    {
        StreamContext* sc = getStreamContext(mh->chunkStreamId);

        if(sc == NULL)
        {
            pair<int, StreamContext*> p(mh->chunkStreamId, new StreamContext());
            sc = p.second;
            sc->prevMsg = mh;
            streamContexts_.push_back(p);
        }

        // Determin timedelta

        // 3 after 0, then timedelta should be type 0's timestamp
        if(sc->prevMsg->chunkType == 0 && mh->chunkType == 3)
        {
            sc->timestampDelta = sc->prevMsg->timestamp;
        }
        else if(mh->chunkType == 1 || mh->chunkType == 2)
        {
            sc->timestampDelta = mh->timestamp;
        }
        else
        {
            //throw RtmpBadProtocalData("can't determin timedelta");
        }

        // 2. determin timestamp
        //    if not type 0, then determin other parameter 
        if(mh->chunkType == 0)
        {
            sc->timestamp = mh->timestamp;
        }
        else
        {
            sc->timestamp += sc->timestampDelta;

            mh->timestamp = sc->timestamp;

            mh->streamId = sc->prevMsg->streamId;

            if(mh->extendtedTimestamp == -1 && mh->chunkType == 3)
            {
                mh->extendtedTimestamp = sc->prevMsg->extendtedTimestamp;
            }

            // type 1 has message length and typeId
            if(mh->chunkType != 1)
            {
                mh->length = sc->prevMsg->length;
                mh->typeId = sc->prevMsg->typeId;
            }
        }

        sc->prevMsg = mh;
    }
    
    mh->unParsedSize = mh->length;
}

RtmpMsgHeaderPtr RtmpParser::parseMsgHeader(int chunkSize)
{
    RTMP_LOG(LEVDEBUG, "RtmpParser::parseMsgHeader public\n");
    if(msgsBuffer_.size() > 0)
    {
        RTMP_LOG(LEVDEBUG, "RtmpParser::parseMsgHeader public, got msg from buffer\n");
        RtmpMsgHeaderPtr h = msgsBuffer_[0];

        msgsBuffer_.erase(msgsBuffer_.begin());
        return h;
    }
    else
    {
        return parseMsgHeader(chunkSize, NULL);
    }
}

RtmpMsgHeaderPtr RtmpParser::parseMsgHeader(int chunkSize, RtmpMsgHeaderPtr* parsingMsgHeader)
{
    RTMP_LOG(LEVDEBUG, "RtmpParser::parseMsgHeader\n");
    RtmpMsgHeaderPtr mh;
    if(parsingMsgHeader == NULL)
    {
        mh = parseMsgHeaderOnly();

        if(mh->length == 528)
        {
            int y = 20;
        }

        onMsgHeaderParsed(mh);
    }
    else
    {
        mh = *parsingMsgHeader; 
    }

    if(chunkSize >= mh->length)
    {
        mh->body = rb_->readBytes(mh->length);
        mh->unParsedSize = 0;
        return mh;
    }
    else
    {
        uint8_t* tmp;

        if(!mh->body)
        {
            mh->body = new uint8_t[mh->length];
        }

        tmp = rb_->readBytes(chunkSize);
        mh->appendData(tmp, chunkSize);
        delete[] tmp;

        if(mh->chunkStreamId > 10)
        {
            throw RtmpBadProtocalData("invalid chunkStreamId");
        }

        int size = chunkSize;

        while(mh->unParsedSize > 0)
        {
            parseMsgHeaderPartial(chunkSize, mh);
        }

        return mh;
    }
}

ConnectCmdObjKey RtmpParser::CmdConnectIsKeyValid(string& keyName)
{
    ConnectCmdObjKey cckValidEnums[CCK_VALID_KEYS_NUM] = {CCK_App, CCK_Flashver, CCK_SwfUrl, CCK_TcUrl, CCK_Type, CCK_Fpad,
                                                         CCK_AudioCodecs, CCK_VideoCodecs, CCK_PageUrl, CCK_ObjectEncoding};
    string cckValidKeys[CCK_VALID_KEYS_NUM] = {"app", "flashver", "swfUrl", "tcUrl", "type", "fpad", "audioCodecs", "videoCodecs", "pageUrl", "objectEncoding"};

    for(int i = 0; i < CCK_VALID_KEYS_NUM; i++)
    {
        if(boost::iequals(keyName, cckValidKeys[i]))
        {
            return cckValidEnums[i];
        }
    }

    return CCK_Unknown;
}

WindowAckSizeMsgPtr RtmpParser::parseWindowAckSizeMsg(RtmpMsgHeaderPtr& mh)
{
    WindowAckSizeMsgPtr acp(new WindowAckSizeMsg());

    ReadBufferPtr readBuffer(new ReadBuffer(mh->length));
    readBuffer->appendData(mh->body, mh->length);

    acp->windowAckSize = readBuffer->read<int32_t>(ReadBuffer::BIG);

    return acp;
}

AMF0Commands RtmpParser::peekAMF0Cmd(RtmpMsgHeaderPtr& mh)
{
    if(mh->length < 0)
    {
        throw RtmpBadProtocalData("body length should >= 0");
    }

    ReadBufferPtr readBuffer(new ReadBuffer(mh->length));
    readBuffer->appendData(mh->body, mh->length);

    AMF0ParserPtr ap(new AMF0Parser(readBuffer));

    try
    {
        string cmd = ap->parseAsString();    

        if(cmd.compare("releaseStream") == 0)
        {
            return AMF0_ReleaseStream;
        }
        else if(cmd.compare("FCPublish") == 0)
        {
            return AMF0_FCPublish;
        }
        else if(cmd.compare("createStream") == 0)
        {
            return AMF0_CreateStream;
        }
        else if(cmd.compare("publish") == 0)
        {
            return AMF0_Publish;
        }
        else if(cmd.compare("connect") == 0)
        {
            return AMF0_Connect;
        }
        else
        {
            throw RtmpNotSupported("amf0 command is not supported. CMD: " + cmd);
        }
    }
    catch(RtmpNoEnoughData& e)
    {
        throw RtmpBadProtocalData("length and body do not match");
    }
    catch(RtmpInvalidAMFData& ae)
    {
        throw RtmpBadProtocalData("peekAMF0Cmd, data is corrupted");
    }
}

AMF0DataTypes RtmpParser::peekAMF0DataType(RtmpMsgHeaderPtr& mh)
{
    if(mh->length < 0)
    {
        throw RtmpBadProtocalData("body length should >= 0");
    }

    ReadBufferPtr readBuffer(new ReadBuffer(mh->length));
    readBuffer->appendData(mh->body, mh->length);

    AMF0ParserPtr ap(new AMF0Parser(readBuffer));

    try
    {
        string cmd = ap->parseAsString();    

        if(cmd.compare("@setDataFrame") == 0)
        {
            return AMF0_DATA_SetDataFrame;
        }
        else
        {
            throw RtmpNotSupported(cmd.c_str());
        }
    }
    catch(RtmpNoEnoughData& e)
    {
        throw RtmpBadProtocalData("length and body do not match");
    }
    catch(RtmpInvalidAMFData& ae)
    {
        throw RtmpBadProtocalData("peekAMF0DataType, data is corrupted");
    }
}

ReleaseStreamCmdPtr RtmpParser::parseReleaseStreamCmd(RtmpMsgHeaderPtr& mh)
{
    if(mh->length < 0)
    {
        throw RtmpBadProtocalData("body length should >=0");
    }

    ReadBufferPtr readBuffer(new ReadBuffer(mh->length));
    readBuffer->appendData(mh->body, mh->length);

    ReleaseStreamCmdPtr mp(new ReleaseStreamCmd());
    AMF0ParserPtr ap(new AMF0Parser(readBuffer));

    try
    {
        string commandName = ap->parseAsString();

        if(commandName != "releaseStream")
        {
            throw RtmpBadProtocalData("expect release stream command");
        }

        mp->transactionId = ap->parseAsNumber();

        AMF0Types t = ap->getNextType(false);

        if(t == AMF0_Null)
        {
            ap->parseNull();
        }
        else if(t == AMF0_Object)
        {
            // skip the object, we do not care
            ap->skip(t);
        }

        mp->streamName = ap->parseAsString();
        return mp;
    }
    catch(RtmpNoEnoughData& e)
    {
        throw RtmpBadProtocalData("length and body do not match");
    }
    catch(RtmpInvalidAMFData& ae)
    {
        throw RtmpBadProtocalData("parseReleaseStreamCmd, data is corrupted");
    }
}

PublishCmdPtr RtmpParser::parsePublishCmd(RtmpMsgHeaderPtr& mh)
{
    if(mh->length < 0)
    {
        throw RtmpBadProtocalData("body length should >=0");
    }

    ReadBufferPtr readBuffer(new ReadBuffer(mh->length));
    readBuffer->appendData(mh->body, mh->length);

    PublishCmdPtr mp(new PublishCmd());
    AMF0ParserPtr ap(new AMF0Parser(readBuffer));

    try
    {
        string commandName = ap->parseAsString();

        if(commandName != "publish")
        {
            throw RtmpBadProtocalData("expect publish command");
        }

        mp->transactionId = ap->parseAsNumber();
        ap->parseNull();

        mp->publishingName = ap->parseAsString();
        mp->publishingType = ap->parseAsString();

        return mp;
    }
    catch(RtmpNoEnoughData& e)
    {
        throw RtmpBadProtocalData("length and body do not match");
    }
    catch(RtmpInvalidAMFData& ae)
    {
        throw RtmpBadProtocalData("parsePublishCmd data is corrupted");
    }
}

FCPublishCmdPtr RtmpParser::parseFCPublishCmd(RtmpMsgHeaderPtr& mh)
{
    if(mh->length < 0)
    {
        throw RtmpBadProtocalData("body length should >=0");
    }

    ReadBufferPtr readBuffer(new ReadBuffer(mh->length));
    readBuffer->appendData(mh->body, mh->length);

    FCPublishCmdPtr mp(new FCPublishCmd());
    AMF0ParserPtr ap(new AMF0Parser(readBuffer));

    try
    {
        string commandName = ap->parseAsString();

        if(commandName != "FCPublish")
        {
            throw RtmpBadProtocalData("expect FCPublishCmd");
        }

        mp->transactionId = ap->parseAsNumber();

        AMF0Types t = ap->getNextType(false);

        if(t == AMF0_Null)
        {
            ap->parseNull();
        }
        else if(t == AMF0_Object)
        {
            // skip the object, we do not care
            ap->skip(t);
        }

        mp->streamName = ap->parseAsString();
        return mp;
    }
    catch(RtmpNoEnoughData& e)
    {
        throw RtmpBadProtocalData("length and body do not match");
    }
    catch(RtmpInvalidAMFData& ae)
    {
        throw RtmpBadProtocalData("parseFCPulishCmd, data is corrupted");
    }
}

CreateStreamCmdPtr RtmpParser::parseCreateStreamCmd(RtmpMsgHeaderPtr& mh)
{
    if(mh->length < 0)
    {
        throw RtmpBadProtocalData("body length should >=0");
    }

    ReadBufferPtr readBuffer(new ReadBuffer(mh->length));
    readBuffer->appendData(mh->body, mh->length);

    CreateStreamCmdPtr mp(new CreateStreamCmd());
    AMF0ParserPtr ap(new AMF0Parser(readBuffer));

    try
    {
        string commandName = ap->parseAsString();

        if(commandName != "createStream")
        {
            throw RtmpBadProtocalData("expect createStream");
        }

        mp->transactionId = ap->parseAsNumber();

        AMF0Types t = ap->getNextType(false);

        if(t == AMF0_Null)
        {
            ap->parseNull();
        }
        else if(t == AMF0_Object)
        {
            // skip the object, we do not care
            ap->skip(t);
        }

        return mp;
    }
    catch(RtmpNoEnoughData& e)
    {
        throw RtmpBadProtocalData("length and body do not match");
    }
    catch(RtmpInvalidAMFData& ae)
    {
        throw RtmpBadProtocalData("parseCreateStreamCmd, data is corrupted");
    }
}

ConnectCmdPtr RtmpParser::parseConnectCmd(RtmpMsgHeaderPtr& mh)
{
    if(mh->length < 0)
    {
        throw RtmpBadProtocalData("body length should >=0");
    }

    ReadBufferPtr readBuffer(new ReadBuffer(mh->length));
    readBuffer->appendData(mh->body, mh->length);

    ConnectCmdPtr mp(new ConnectCmd());
    AMF0ParserPtr ap(new AMF0Parser(readBuffer));

    try
    {
        string commandName = ap->parseAsString();

        if(commandName != "connect")
        {
            throw RtmpBadProtocalData("expect connect command");
        }

        mp->transactionId = ap->parseAsNumber();  

        if(mp->transactionId != 1)
        {
            throw RtmpBadProtocalData("transactionId is not 1 in connect");
        }

        // parse command object
        AMF0Types t = ap->getNextType(false);

        if(t != AMF0_Object)
        {
            throw RtmpBadProtocalData("RtmpParser::parseConnectCmd, Expect Command Object");
        } 
        ap->skipObjectStart();

        bool parseKey = true;
        string keyName;
        ConnectCmdObjKey key;
        while(!ap->isFinished())
        {
            t = ap->getNextType(true);

            if(t == AMF0_ObjectEnd)
            {
                // done
                ap->skipObjectEnd();
                break;
            }

            if(parseKey)
            {
                keyName = ap->parseObjectKey();
                key = CmdConnectIsKeyValid(keyName);
            }
            else
            {
                switch(key)
                {
                    case CCK_App:
                        mp->app = ap->parseAsString();
                        break;
                    case CCK_Flashver:
                        mp->flashver = ap->parseAsString();
                        break;
                    case CCK_SwfUrl:
                        mp->swfUrl = ap->parseAsString();
                        break;
                    case CCK_TcUrl:
                        mp->tcUrl = ap->parseAsString();
                        break;
                    case CCK_Type:
                        mp->type = ap->parseAsString();
                        break;
                    case CCK_Fpad:
                        mp->fpad = ap->parseAsBool();
                        break;
                    case CCK_AudioCodecs:
                        mp->audioCodecs = (AudioCodecConst)ap->parseAsNumber();
                        break;
                    case CCK_VideoCodecs:
                        mp->videoCodecs = (VideoCodecConst)ap->parseAsNumber();
                        break;
                    case CCK_PageUrl:
                        mp->pageUrl = ap->parseAsString();
                        break;
                    case CCK_ObjectEncoding:
                        mp->objectEncoding = (ObjectEncodingConst)ap->parseAsNumber();
                        break;
                    default:
                        RTMP_LOG(LEVDEBUG, "RtmpParser::parseConnectCmd: not interested key %s\n", keyName.c_str());
                        ap->skip(t);
                        break;
                }
            }

            parseKey = !parseKey;
        }

        return mp;
    }
    catch(RtmpNoEnoughData& e)
    {
        throw RtmpBadProtocalData("length and body do not match");
    }
    catch(RtmpInvalidAMFData& ae)
    {
        throw RtmpBadProtocalData("parseConnectCmd, data is corrupted");
    }
}

MetaDataMsgPtr RtmpParser::parseMetaData(RtmpMsgHeaderPtr& mh)
{
    if(mh->length < 0)
    {
        throw RtmpBadProtocalData("body length should >=0");
    }

    ReadBufferPtr readBuffer(new ReadBuffer(mh->length));
    readBuffer->appendData(mh->body, mh->length);

    MetaDataMsgPtr mp(new MetaDataMsg());
    AMF0ParserPtr ap(new AMF0Parser(readBuffer));

    bool parseKey = true;
    string keyName;
    try
    {
        string dataName = ap->parseAsString();

        if(dataName != "@setDataFrame")
        {
            throw RtmpBadProtocalData("expect @setDataFrame");
        }

        // set raw data
        mp->metadata = readBuffer->getUnReadBuffer();
        mp->metadata_size = readBuffer->getUnReadSize();

        if(ap->parseAsString() != "onMetaData")
        {
            throw RtmpBadProtocalData("expect onMetaData");
        }

        // parse command object
        AMF0Types t = ap->getNextType(false);

        if(t == AMF0_Object)
        {
            ap->skipObjectStart();
        }
        else if(t == AMF0_EcmaArray)
        {
            ap->skipEcmaArrayStart();
        }
        else
        {
            throw RtmpBadProtocalData("RtmpParser::parseMetaData, Expect Command Object or ECMA array");
        }

        while(!ap->isFinished())
        {
            t = ap->getNextType(true);

            if(t == AMF0_ObjectEnd)
            {
                // done
                ap->skipObjectEnd();
                break;
            }

            if(parseKey)
            {
                keyName = ap->parseObjectKey();
            }
            else
            {
                if(keyName == "author")
                {
                    mp->author = ap->parseAsString();
                }
                else if(keyName == "copyright")
                {
                    mp->copyright = ap->parseAsString();
                }
                else if(keyName == "description")
                {
                    mp->description = ap->parseAsString();
                }
                else if(keyName == "keywords")
                {
                    mp->keywords = ap->parseAsString();
                }
                else if(keyName == "rating")
                {
                    mp->rating = ap->parseAsString();
                }
                else if(keyName == "title")
                {
                    mp->title = ap->parseAsString();
                }
                else if(keyName == "presetname")
                {
                    mp->presetname = ap->parseAsString();
                }
                else if(keyName == "creationdate")
                {
                    mp->creationdate = ap->parseAsString();
                }
                else if(keyName == "videodevice")
                {
                    mp->videodevice = ap->parseAsString();
                }
                else if(keyName == "framerate")
                {
                    mp->framerate = (int)ap->parseAsNumber();
                }
                else if(keyName == "width")
                {
                    mp->width = ap->parseAsNumber();
                }
                else if(keyName == "height")
                {
                    mp->height = ap->parseAsNumber();
                }
                else if(keyName == "videocodecid")
                {
                    if(t == AMF0_Number)
                    {
                        // ffmpeg sent this as number
                        mp->videocodecid = Utility::numToStr(ap->parseAsNumber());
                    }
                    else
                    {
                        mp->videocodecid = ap->parseAsString();
                    }
                }
                else if(keyName == "videodatarate")
                {
                    mp->videodatarate = ap->parseAsNumber();
                }
                else if(keyName == "avclevel")
                {
                    mp->avclevel = (int)ap->parseAsNumber();
                }
                else if(keyName == "avcprofile")
                {
                    mp->avcprofile = (int)ap->parseAsNumber();
                }
                else if(keyName == "videokeyframe_frequency")
                {
                    mp->videokeyframe_frequency = (int)ap->parseAsNumber();
                }
                else if(keyName == "audiodevice")
                {
                    mp->audiodevice = ap->parseAsString();
                }
                else if(keyName == "audiosamplerate")
                {
                    mp->audiosamplerate = ap->parseAsNumber();
                }
                else if(keyName == "audiochannels")
                {
                    mp->audiochannels = (int)ap->parseAsNumber();
                }
                else if(keyName == "audioinputvolume")
                {
                    mp->audioinputvolume = (int)ap->parseAsNumber();
                }
                else if(keyName == "audiocodecid")
                {
                    if(t == AMF0_Number)
                    {
                        // ffmpeg sent this as number
                        mp->audiocodecid = Utility::numToStr(ap->parseAsNumber());
                    }
                    else
                    {
                        mp->audiocodecid = ap->parseAsString();
                    }
                }
                else if(keyName == "audiodatarate")
                {
                    mp->audiodatarate = ap->parseAsNumber();
                }
                else
                {
                    ap->skip(t);
                    RTMP_LOG(LEVWARN, "onMetaData: ignore unknown key %s\n", keyName.c_str());
                }
            }

            parseKey = !parseKey;
        }

        return mp;
    }
    catch(RtmpNoEnoughData& e)
    {
        throw RtmpBadProtocalData("length and body do not match");
    }
    catch(RtmpInvalidAMFData& ae)
    {
        throw RtmpBadProtocalData(("parseMetaData, data is corrupted. key: " + keyName).c_str());
    }
}

