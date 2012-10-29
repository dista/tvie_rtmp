#ifndef READ_BUFFER_H
#define READ_BUFFER_H

#include "rtmpexception.h"
#include <stdint.h>
#include <boost/shared_ptr.hpp>

class ReadBuffer
{
    private:
        uint8_t* buffer_;
        int capacity_;
        int cout_;
        int bi_;

        bool inSnap_;
        int snapBi_;

    public:
        enum Mode
        {
            BIG,
            LITTLE
        };

        int getCount()
        {
            return cout_;
        }
        int getBi()
        {
            return bi_;
        }


        ReadBuffer(int capacity);
        ~ReadBuffer();

        void appendData(uint8_t* data, int size);

        uint8_t readByte();
        uint8_t* readBytes(int size);

        // it will append 0 at last
        char* readChars(int size);

        void putToAnotherBuffer(ReadBuffer* readBuffer, int size);

        int getUnReadSize();
        uint8_t* getUnReadBufferNoCopy();
        uint8_t* getUnReadBuffer();
        void skip(int bytes);

        template <class T>
        static T read(uint8_t* data, int size, ReadBuffer::Mode mode)
        {
            boost::shared_ptr<ReadBuffer> rp(new ReadBuffer(size));
            rp->appendData(data, size);
            return rp->read<T>(mode);
        }

        template <class T>
        T read(ReadBuffer::Mode mode)
        {
            return read<T>(mode, sizeof(T));
        }

        template <class T>
        T peek(ReadBuffer::Mode mode)
        {
            int orig_bi = bi_;
            try
            {
                T v = read<T>(mode, sizeof(T));
                bi_ = orig_bi;
                return v;
            }
            catch(RtmpNoEnoughData& e)
            {
                bi_ = orig_bi;
                throw e;
            }
        }

        template <class T>
        T peek(ReadBuffer::Mode mode, int bytesCount)
        {
            int orig_bi = bi_;
            try
            {
                T v = read<T>(mode, bytesCount);
                bi_ = orig_bi;
                return v;
            }
            catch(std::exception& e)
            {
                bi_ = orig_bi;
                throw e;
            }
        }

        template <class T>
        T read(ReadBuffer::Mode mode, int bytesCount)
        {
            int size = sizeof(T);

            if(bytesCount > size)
            {
                throw RtmpInvalidArg("bytesCount");
            }
            
            if(bytesCount < 0)
            {
                throw RtmpInvalidArg("bytesCount");
            }

            size = bytesCount;

            T v = 0;
            if(getUnReadSize() < size)
            {
                throw RtmpNoEnoughData();
            }

            for(int i = 0; i < size; i++)
            {
                if(mode == ReadBuffer::BIG)
                {
                    v |= (T)readByte() << (8 * (size - i - 1));
                }
                else
                {
                    v |= (T)readByte() << (8 * i);
                }
            }

            return v; 
        }

        // start snap reading
        void snapStart();
        // stop snap reading
        void snapStop();
        // clear snap
        void snapClear();

        void reset();
};

typedef boost::shared_ptr<ReadBuffer> ReadBufferPtr;

#endif
