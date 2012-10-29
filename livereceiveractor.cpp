#include "livereceiveractor.h"

bool LiveReceiverActor::initialized = false;
string LiveReceiverActor::urlPrefix = "";
string LiveReceiverActor::fmt = "";

void StreamSetupInfo::writeFlvHeader()
{
    boost::lock_guard<boost::mutex> lk(mt_);

    uint8_t flv[9];
    memset(flv, 0, 9);

    flv[0] = 'F';
    flv[1] = 'L';
    flv[2] = 'V';
    flv[3] = 1; // version 1
    
    uint8_t flag = 0;
    flag |= hasVideo ? 0x1 : 0;
    flag |= hasAudio ? 0x4 : 0;

    flv[4] = flag;
    flv[8] = 9; // header size
    
    rb_.appendData(flv, 9);
    writeTagSize(0);

    flvHeaderWritten_ = true;
}

bool StreamSetupInfo::isFlvHeaderWritten()
{
    return flvHeaderWritten_;
}

bool StreamSetupInfo::isInputCreated()
{
    return inCtx_ != NULL;
}

AVFormatContext* StreamSetupInfo::getFormatContext()
{
    return inCtx_;
}

void StreamSetupInfo::writeTagSize(int32_t tagSize)
{
    wb_.reInit();
    wb_.writeB((int32_t)tagSize);

    rb_.appendData(wb_.getBufferPtr(), wb_.getBufferCount());
}

void StreamSetupInfo::writeData(RtmpMsgHeaderPtr& msg)
{
    boost::lock_guard<boost::mutex> lk(mt_);

    wb_.reInit();
    wb_.writeB(0, 3);
    wb_.writeB(msg->typeId, 5);
    wb_.writeB(msg->length, 24);
    wb_.writeB(msg->timestamp, 24);
    wb_.writeB(msg->timestamp >> 24, 8);
    wb_.writeB(0, 24);

    wb_.writeBytes(msg->body, msg->length);
    wb_.writeB((int32_t)(msg->length + 11));

    rb_.appendData(wb_.getBufferPtr(), wb_.getBufferCount());
}

void StreamSetupInfo::writeMetaData(MetaDataMsgPtr& meta)
{
    boost::lock_guard<boost::mutex> lk(mt_);

    wb_.reInit();
    wb_.writeB(0, 3);
    wb_.writeB(0x12, 5);
    wb_.writeB(meta->metadata_size, 24);
    wb_.writeB(meta->timestamp, 24);
    wb_.writeB(meta->timestamp >> 24, 8);
    wb_.writeB(0, 24);

    wb_.writeBytes(meta->metadata, meta->metadata_size);
    wb_.writeB((int32_t)(meta->metadata_size + 11));

    rb_.appendData(wb_.getBufferPtr(), wb_.getBufferCount());
}

void StreamSetupInfo::setEndOfFile()
{
    boost::lock_guard<boost::mutex> lk(mt_);

    endOfFile_ = true;
}

// TODO: set endoffile
int StreamSetupInfo::feedData(uint8_t *buf, int buf_size)
{
    boost::unique_lock<boost::mutex> lk(mt_);
    lk.unlock();

    int wait_time = 0;
    
    while(true)
    {
        lk.lock();
        int size = rb_.getUnReadSize();
        if(size == 0)
        {
            if(error_)
            {
                return -1;
            }

            if(endOfFile_)
            {
                // tell ffmpeg we are done
                return 0;
            }
            lk.unlock();
            usleep(10);
            wait_time += 10;

            if(wait_time > 10000000) // 10 seconds
            {
                RTMP_LOG(LEVERROR, "wait data timeout\n");
                return -1;
            }
            continue;
        }
        else if(size < buf_size)
        {
            memcpy(buf, rb_.getUnReadBufferNoCopy(), size);
            // all data is read 
            rb_.reset();
            return size;
        }
        else
        {
            memcpy(buf, rb_.getUnReadBufferNoCopy(), buf_size);
            rb_.skip(buf_size);
            return buf_size;
        }
    }
}

StreamSetupInfo::~StreamSetupInfo()
{
    if(inCtx_)
    {
        if(inCtx_->pb)
        {
            av_free(inCtx_->pb);
        }

        avformat_close_input(&inCtx_);
        inCtx_ = NULL;
    }
}

// Must be called in main
void LiveReceiverActor::Init(string urlPrefix, string fmt)
{
    if(LiveReceiverActor::initialized)
    {
        return;
    }

    LiveReceiverActor::urlPrefix = urlPrefix;
    LiveReceiverActor::fmt = fmt;
    av_register_all();
    avformat_network_init();

    LiveReceiverActor::initialized = true;
}

void LiveReceiverActor::notifyPushThreadDone()
{
    boost::lock_guard<boost::mutex> gl(mt_);

    isPushThreadDone_ = true;
}

bool LiveReceiverActor::isPushThreadDone()
{
    boost::lock_guard<boost::mutex> gl(mt_);

    return isPushThreadDone_;
}

//global urlPrefix: path
LiveReceiverActor::LiveReceiverActor():
    ctx_(NULL),
    streamInfoCount_(0),
    avStreamIndex_(0),
    headerWritten_(false),
    startTime_(-1),
    th_(NULL),
    pkt_(NULL),
    mt_(),
    isPushThreadDone_(false)

{
    if(!LiveReceiverActor::initialized)
    {
        throw RtmpInternalError("LiveReceiverActor is not initialized, pleace call LiveReceiverActor::Init first!");
    }
}

LiveReceiverActor::~LiveReceiverActor()
{
    delete th_;

    if(pkt_)
    {
        if(pkt_->data)
        {
            av_free_packet(pkt_);
        }
        delete pkt_;
    }

    for(int i = 0; i < streamInfoCount_; i++)
    {
        delete streamInfos_[i];
        streamInfos_[i] = NULL; 
    }

    if(headerWritten_)
    {
        av_write_trailer(ctx_);
    }

    if(ctx_)
    {
        if(ctx_->pb)
        {
            avio_close(ctx_->pb);
            ctx_->pb = NULL;
        }
        avformat_free_context(ctx_);
        ctx_ = NULL;
    }
}

RtmpActor* LiveReceiverActor::createActor()
{
    return new LiveReceiverActor();
}

bool LiveReceiverActor::onConnect(ConnectCmdPtr cmd)
{
    connectInfo_ = cmd;
    return true;
}

void LiveReceiverActor::onDisconnect()
{
    if(streamInfos_[0])
    {
        streamInfos_[0]->setEndOfFile();
    }

    if(th_)
    {
        // wait for 500 milliseconds at most, otherwise we kill the thread
        th_->timed_join(boost::posix_time::milliseconds(500));
        th_->interrupt();
    }
}

StreamSetupInfo* LiveReceiverActor::findStreamSetupInfo(int streamId)
{
    for(int i = 0; i < streamInfoCount_; i++)
    {
        if(streamInfos_[i]->streamId == streamId)
        {
            return streamInfos_[i];
        }
    }

    return NULL;
}

void StreamSetupInfo::createInput()
{
    inCtx_ = avformat_alloc_context();

    if(inCtx_ == NULL)
    {
        throw RtmpInternalError("alloc input context failed");
    }

    if(!(inputIOBuffer_ = (uint8_t*)av_malloc(StreamSetupInfo::INPUT_IO_BUFFER_SIZE)))
    {
        throw RtmpInternalError("alloc buffer failed");
    }

    inCtx_->pb = avio_alloc_context(inputIOBuffer_, StreamSetupInfo::INPUT_IO_BUFFER_SIZE,
            0, (void*)this, 
            (int (*)(void*, uint8_t*, int))&RtmpPushProtol::read_packet, //read callback
            NULL, //write callback
            NULL  //seek callback
            );
    if(!inCtx_->pb)
    {
        throw RtmpInternalError("alloc input io context failed");
    }
}
        
bool LiveReceiverActor::onPublish(int streamId, string publishUrl)
{
    StreamSetupInfo* info = NULL;
    if(!(info = findStreamSetupInfo(streamId)))
    {
        throw RtmpInternalError("can't find stream id");
    }

    //TODO: currently we only handle one stream
    if(info != streamInfos_[0])
    {
        return true;
    }

    outputUrl_ = LiveReceiverActor::urlPrefix + "/"
                       + connectInfo_->app + "/" + publishUrl 
                       + LiveReceiverActor::fmt;

      
    ctx_ = avformat_alloc_context();

    if(avio_open(&ctx_->pb, outputUrl_.c_str(), AVIO_FLAG_WRITE) != 0)
    {
        throw RtmpInternalError(("failed to open: " + outputUrl_).c_str());
    }
    
    if((ctx_->oformat = av_guess_format(NULL, outputUrl_.c_str(), NULL))
       == NULL)
    {
        throw RtmpInternalError("failed to guess format");
    }

    return true;
}
        
bool LiveReceiverActor::onCreateStream(int nextStreamId)
{
    if(streamInfoCount_ > LiveReceiverActor::STREAM_COUNT)
    {
        throw RtmpInternalError("too many streams");
    }

    streamInfos_[streamInfoCount_++] = new StreamSetupInfo(nextStreamId);
    return true;
}

bool LiveReceiverActor::onMetaData(int streamId, MetaDataMsgPtr metaData)
{
    metaData_ = metaData;
    StreamSetupInfo* info = NULL;
    if(!(info = findStreamSetupInfo(streamId)))
    {
        throw RtmpInternalError("onMetaData, failed to find StreamSetupInfo");
    }

    //TODO: currently we only handle one stream
    if(info != streamInfos_[0])
    {
        return true;
    }

    info->hasVideo = metaData->width != -1;
    info->hasAudio = metaData->audiosamplerate != -1;


    return true;
}

bool LiveReceiverActor::onReceiveStream(int streamId, bool isVideo, RtmpMsgHeaderPtr msg)
{
    // write thread is done, we do not need more data. so exit!
    if(isPushThreadDone())
    {
        throw RtmpInternalError("Write ends, no more data needed");
    }

    StreamSetupInfo* info = NULL;
    if(!(info = findStreamSetupInfo(streamId)))
    {
        throw RtmpInternalError("onReceiveStream, failed to find streamId");
    }

    //TODO: currently we only handle one stream
    if(info != streamInfos_[0])
    {
        return true;
    }

    if(!info->isFlvHeaderWritten())
    {
        info->writeFlvHeader();

        // write meta data
        info->writeMetaData(metaData_);
    }

    if(!th_)
    {
        th_ = new boost::thread(boost::bind(&LiveReceiverActor::pushThread, this));
    }

    info->writeData(msg);

    return true;
}

bool LiveReceiverActor::setOutputCtx(AVFormatContext* inCtx)
{
    AVStream* inVideoStream = NULL;
    AVStream* inAudioStream = NULL;

    for(int i = 0; i < inCtx->nb_streams; i++)
    {
        if(inCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            inVideoStream = inCtx->streams[i];
        }
        else if(inCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            inAudioStream = inCtx->streams[i];
        }
    }

    int streamIndex = 0;
    if(inVideoStream && !av_new_stream(ctx_, streamIndex))
    {
        RTMP_LOG(LEVERROR, "av_new_stream error\n");
        return false;
    }
    if(inVideoStream)
    {
        streamMap_[inVideoStream->index] = streamIndex;
        ctx_->streams[streamIndex]->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        streamIndex++;
    }

    if(inAudioStream && !av_new_stream(ctx_, streamIndex))
    {
        RTMP_LOG(LEVERROR, "av_new_stream error\n");
        return false;
    }

    if(inAudioStream)
    {
        streamMap_[inAudioStream->index] = streamIndex;
        ctx_->streams[streamIndex]->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        streamIndex++;
    }

    for(int i = 0; i < ctx_->nb_streams; i++)
    {
        AVCodecContext* codecCtx = ctx_->streams[i]->codec;
        AVStream* inStream = (codecCtx->codec_type == AVMEDIA_TYPE_VIDEO) ? 
                            inVideoStream : inAudioStream;
        AVCodecContext* inCodecCtx = inStream->codec;

        ctx_->streams[i]->disposition = inStream->disposition;

        codecCtx->bits_per_raw_sample = inCodecCtx->bits_per_raw_sample;
        codecCtx->chroma_sample_location = inCodecCtx->chroma_sample_location;
        codecCtx->codec_id = inCodecCtx->codec_id;

        if(!codecCtx->codec_tag)
        {
            if(!ctx_->oformat->codec_tag ||
                av_codec_get_id(ctx_->oformat->codec_tag, inCodecCtx->codec_tag) == codecCtx->codec_id ||
                av_codec_get_tag(ctx_->oformat->codec_tag, inCodecCtx->codec_id) <= 0)
            {
                codecCtx->codec_tag = inCodecCtx->codec_tag;
            }
        }

        if(inCodecCtx->extradata_size > 0)
        {
            codecCtx->extradata = (uint8_t*)av_mallocz(inCodecCtx->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
            memcpy(codecCtx->extradata, inCodecCtx->extradata, inCodecCtx->extradata_size);
            codecCtx->extradata_size = inCodecCtx->extradata_size;
            codecCtx->time_base = inStream->time_base;
        }

        if(codecCtx->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            codecCtx->pix_fmt = inCodecCtx->pix_fmt;
            codecCtx->width   = inCodecCtx->width;
            codecCtx->height  = inCodecCtx->height;
            codecCtx->has_b_frames = inCodecCtx->has_b_frames;
        }
        else if(codecCtx->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            codecCtx->channel_layout = inCodecCtx->channel_layout;
            codecCtx->sample_rate = inCodecCtx->sample_rate;
            codecCtx->channels = inCodecCtx->channels;
            codecCtx->frame_size = inCodecCtx->frame_size;
            codecCtx->block_align = inCodecCtx->block_align;
        }

        if(ctx_->oformat->flags & AVFMT_GLOBALHEADER)
        {
            codecCtx->flags |= CODEC_FLAG_GLOBAL_HEADER;
        }
    }

    return true;
}

void LiveReceiverActor::pushThread()
{
    // stream info is already done
    //
    StreamSetupInfo* info = streamInfos_[0];
    info->createInput();

    AVFormatContext* context = info->getFormatContext();

    if(avformat_open_input(&context, NULL, NULL, NULL) < 0)
    {
        RTMP_LOG(LEVERROR, "open input failed\n");
    }

    if(avformat_find_stream_info(context, NULL) < 0)
    {
        RTMP_LOG(LEVERROR, "find stream info error\n");
    }
    
    //copy context
    setOutputCtx(context);

    if(avformat_write_header(ctx_, NULL) < 0)
    {
        RTMP_LOG(LEVERROR, "write header failed\n");
    }

    headerWritten_ = true;

    pkt_ = new AVPacket();

    int ret;
    AVStream* inStream;
    AVStream* outStream;

    while(true)
    {
        av_init_packet(pkt_);
        if((ret = av_read_frame(context, pkt_)) != 0)
        {
            if(ret == AVERROR(EAGAIN))
            {
                continue;
            }   

            if(ret != AVERROR_EOF)
            {
                RTMP_LOG(LEVERROR, "read frame error\n");
            }
            else
            {
                RTMP_LOG(LEVDEBUG, "reached end of file\n");
            }
            break;
        }

        inStream = context->streams[pkt_->stream_index];
        outStream = ctx_->streams[streamMap_[pkt_->stream_index]];

        if(startTime_ == -1)
        {
            startTime_ = av_rescale_q(pkt_->dts, inStream->time_base, outStream->time_base);
        }

        pkt_->pts = av_rescale_q(pkt_->pts, inStream->time_base, outStream->time_base) - startTime_;
        pkt_->dts = av_rescale_q(pkt_->dts, inStream->time_base, outStream->time_base) - startTime_;
        pkt_->duration = av_rescale_q(pkt_->duration, inStream->time_base, outStream->time_base);
        pkt_->stream_index = streamMap_[pkt_->stream_index];

        if(av_interleaved_write_frame(ctx_, pkt_) < 0)
        {
            av_free_packet(pkt_);
            RTMP_LOG(LEVERROR, "write frame error\n");
            break;
        }

        av_free_packet(pkt_);
    }

    notifyPushThreadDone();
}

// it will be called when we read data from our customed AVIOContext
int RtmpPushProtol::read_packet(void *opaque, uint8_t* buf, int buf_size)
{
    StreamSetupInfo* info = (StreamSetupInfo*)opaque;

    info->feedData(buf, buf_size);
}
