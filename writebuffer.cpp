#include "writebuffer.h"
#include <stdio.h>
#include <string.h>

WriteBuffer::WriteBuffer(int32_t size)
{
    if(size < 0)
    {
        throw RtmpInvalidArg("size");
    }

    size_ = size;
    buffer_ = new uint8_t[size_]; 
    memset(buffer_, 0, size_);

    bi_ = 0;
    bits_left_ = 8;
}

WriteBuffer::~WriteBuffer()
{
    delete[] buffer_;
    bi_ = 0;
    bits_left_ = 0;
    size_ = 0;
}

void WriteBuffer::writeBytes(uint8_t* data, int size)
{
    while(size > size_ - bi_ + 1)
    {
        realloc();
    }

    if(bits_left_ == 8)
    {
        memcpy(buffer_ + bi_, data, size);
        bi_ += size;
    }
    else
    {
        int idx = 0;
        while(idx < size)
        {
            buffer_[bi_++] |= data[idx] >> (8 - bits_left_);
            buffer_[bi_] = data[idx] << bits_left_;
        }
    }
}

void WriteBuffer::realloc()
{
    uint8_t* buf = new uint8_t[size_ * 2];
    memset(buf, 0, size_ * 2);
    memcpy(buf, buffer_, bi_);

    delete[] buffer_;
    buffer_ = buf;

    size_ *= 2;
}

void WriteBuffer::realloc_for_write()
{
    // 8 + 1, we at most write bits
    if(bi_ + 9 < size_)
    {
        return;
    }

    realloc();
}

void WriteBuffer::writeByte(uint8_t s, int bits)
{
    if(bits < 0 || bits > 8)
    {
        throw RtmpInvalidArg("bits");
    }

    s = s & ((1 << bits) - 1);

    int left_shift = (bits - bits_left_) > 0 ? bits - bits_left_ : 0;
    buffer_[bi_] = buffer_[bi_] | (s >> left_shift);

    if((bits - bits_left_) >= 0)
    {
        buffer_[++bi_] = s & left_shift;
        bits_left_ = 8 - left_shift;
    }
    else
    {
        bits_left_ -= bits;
    }
}

void WriteBuffer::writeByte(uint8_t s)
{
    writeByte(s, 8);
}

void WriteBuffer::reInit()
{
    memset(buffer_, 0, size_);
    bi_ = 0;
    bits_left_ = 8;
}

uint8_t* WriteBuffer::getBuffer()
{
    uint8_t* tmp = new uint8_t[getBufferCount()];
    memcpy(tmp, buffer_, getBufferCount());
    return tmp;
}

uint8_t* WriteBuffer::getBufferPtr()
{
    return buffer_;
}

int WriteBuffer::getBufferCount()
{
    if(bits_left_ != 8)
    {
        return bi_ + 1;
    }

    return bi_;
}

