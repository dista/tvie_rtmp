#ifndef WRITE_BUFFER_H
#define WRITE_BUFFER_H

#include <stdint.h>
#include "rtmpexception.h"
#include <boost/shared_ptr.hpp>

class WriteBuffer
{
    private:
      uint8_t* buffer_;
      int32_t bi_;
      int32_t bits_left_;
      int32_t size_;

      /*
       * realloc buffer if needed
       */
      void realloc();
      void realloc_for_write();

    public:
      WriteBuffer(int32_t size);
      ~WriteBuffer();

      void writeBytes(uint8_t* data, int size);

      void writeByte(uint8_t s, int bits);
      void writeByte(uint8_t s);

      void reInit();
      uint8_t* getBuffer();
      uint8_t* getBufferPtr();
      int getBufferCount();

      template <class T>
      void writeB(T s, int bits)
      {
          if(bits < 0 || bits > (int)sizeof(T) * 8)
          {
              throw RtmpInvalidArg("bits");
          }

          realloc_for_write();

          uint64_t bits_v = ((uint64_t)1 << bits) - 1;

          if(bits == 64)
          {
              bits_v = 0xffffffff;
          }

          T t = (T)(s & bits_v);

          while(bits > 0)
          {
              int write_bits = (bits - 8) > 0 ? 8 : bits;          
              uint8_t v = t >> (bits - write_bits);                
              writeByte(v, write_bits);                            
                                                                 
              bits -= write_bits;                                 
              t &= (1 << bits) - 1;                             
          }
      }

      template <class T>
      void writeB(T s)
      {
          writeB(s, sizeof(T) * 8);
      }

      template <class T>
      void writeL(T s, int bits)
      {
          if(bits < 0 || bits > sizeof(T) * 8)
          {
              throw RtmpInvalidArg("bits");
          }

          realloc_for_write();

          uint64_t bits_v = ((uint64_t)1 << bits) - 1;

          if(bits == 64)
          {
              bits_v = 0xffffffff;
          }

          T t = (T)(s & bits_v);

          while(bits > 0)
          {
              int write_bits = (bits - 8) > 0 ? 8 : bits;     
              uint8_t v = t & ((1 << write_bits) - 1);          
              t = t >> (bits - write_bits);                   
              writeByte(v, write_bits);                       
                                                            
              bits -= write_bits;                             
              t &= (1 << bits) - 1;            
          }
      }

      template <class T>
      void writeL(T s)
      {
          writeL(s, sizeof(T) * 8);
      }
};

typedef boost::shared_ptr<WriteBuffer> WriteBufferPtr;
#endif
