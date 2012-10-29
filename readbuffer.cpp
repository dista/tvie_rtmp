#include "readbuffer.h"
#include <string.h>
#include "log.h"
#include "rtmpexception.h"

ReadBuffer::ReadBuffer(int capacity)
{
    capacity_ = capacity;
    buffer_ = new uint8_t[capacity];
    cout_ = 0;
    bi_ = 0;
    inSnap_ = false;
    snapBi_ = 0;
}

ReadBuffer::~ReadBuffer()
{
    delete[] buffer_;
    capacity_ = 0;
    cout_ = 0;
    bi_ = 0;
}

void ReadBuffer::reset()
{
    cout_ = 0;
    bi_ = 0;
}

void ReadBuffer::appendData(uint8_t* data, int size)
{
    if(inSnap_)
    {
        throw RtmpNotSupported("do not support append Data in snap");
    }

    bool realloc = false;
    while((cout_ + size) > capacity_)
    {
        realloc = true;

        if((cout_ - bi_ + size) > capacity_)
        {
            capacity_ *= 2;
        }
        else
        {
            break;
        }
    }

    if(realloc)
    {
        uint8_t* new_buf = new uint8_t[capacity_];
        memset(new_buf, 0, capacity_);

        memcpy(new_buf, buffer_ + bi_, cout_ - bi_);
        cout_ -= bi_;
        bi_ = 0;
        delete[] buffer_;
        buffer_ = new_buf;
    }

    memcpy(buffer_ + cout_, data, size);
    cout_ += size;
}

void ReadBuffer::skip(int bytes)
{
    if(getUnReadSize() < bytes)
    {
        throw RtmpNoEnoughData();
    }

    bi_ += bytes;
}

uint8_t ReadBuffer::readByte()
{
    if(getUnReadSize() < 1)
    {
        throw RtmpNoEnoughData();
    }

    return buffer_[bi_++];
}

int ReadBuffer::getUnReadSize()
{
    return cout_ - bi_;
}

uint8_t* ReadBuffer::getUnReadBufferNoCopy()
{
    return buffer_ + bi_;
}

uint8_t* ReadBuffer::getUnReadBuffer()
{
    int size = getUnReadSize();
    uint8_t* buf = readBytes(size);

    bi_ -= size;

    return buf;
}

uint8_t* ReadBuffer::readBytes(int size)
{
    if(getUnReadSize() < size)
    {
        throw RtmpNoEnoughData();
    }

    uint8_t* tmp = new uint8_t[size];
    memcpy(tmp, buffer_ + bi_, size);
    bi_ += size;

    return tmp;
}

char* ReadBuffer::readChars(int size)
{
    if(getUnReadSize() < size)
    {
        throw RtmpNoEnoughData();
    }

    char* tmp = new char[size + 1];
    memcpy(tmp, buffer_ + bi_, size + 1);
    tmp[size] = 0;
    bi_ += size;

    return tmp;
}

void ReadBuffer::putToAnotherBuffer(ReadBuffer* readBuffer, int size)
{
    if(getUnReadSize() < size)
    {
        throw RtmpNoEnoughData();
    }

    readBuffer->appendData(buffer_ + bi_, size);
    bi_ += size;
} 

void ReadBuffer::snapStart()
{
    if(inSnap_)
    {
        throw RtmpInvalidArg("snap is already started");
    }

    inSnap_ = true;
    snapBi_ = bi_;
}

void ReadBuffer::snapStop()
{
    inSnap_ = false;
    bi_ = snapBi_;
}

void ReadBuffer::snapClear()
{
    inSnap_ = false;
}
