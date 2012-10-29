#include "amf0.h"
#include "utility.h"
#include "rtmpexception.h"

AMF0Parser::AMF0Parser(ReadBufferPtr& ptr):
    rb_(ptr)
{
}

string AMF0Parser::parseAsString()
{
    uint8_t tp = rb_->readByte();

    if(tp != AMF0_String)
    {
        throw RtmpInvalidAMFData("expect string");
    }

    uint16_t strLen = rb_->read<uint16_t>(ReadBuffer::BIG);
    char* strData = rb_->readChars(strLen);

    string ret(strData);
    delete[] strData;

    return ret;
}

string AMF0Parser::parseObjectKey()
{
    uint16_t strLen = rb_->read<uint16_t>(ReadBuffer::BIG);
    char* strData = rb_->readChars(strLen);

    string ret(strData);
    delete[] strData;

    return ret;
}

bool AMF0Parser::parseAsBool()
{
    uint8_t tp = rb_->readByte();

    if(tp != AMF0_Boolean)
    {
        throw RtmpInvalidAMFData("expect bool");
    }

    return rb_->readByte() != 0;
}

double AMF0Parser::parseAsNumber() 
{
    uint8_t tp = rb_->readByte();

    if(tp != AMF0_Number)
    {
        throw RtmpInvalidAMFData("expect number");
    }

    uint8_t* bytes = rb_->readBytes(8);
    Utility::reverseBytes(bytes, 8);

    double ret = *((double *)bytes);
    delete[] bytes;

    return ret;
}

void AMF0Parser::parseNull()
{
    uint8_t tp = rb_->readByte();

    if(tp != AMF0_Null)
    {
        throw RtmpInvalidAMFData("expect null");
    }
}

void AMF0Parser::parseUndefined()
{
    uint8_t tp = rb_->readByte();

    if(tp != AMF0_Undefined)
    {
        throw RtmpInvalidAMFData("expect undefined");
    }
}

void AMF0Parser::skipObjectStart()
{
    rb_->skip(1);
} 

void AMF0Parser::skipEcmaArrayStart()
{
    // type + length
    rb_->skip(5);
}

void AMF0Parser::skipObjectEnd()
{
    rb_->skip(3);
}

AMF0Types AMF0Parser::getNextType(bool parsingObject)
{
    uint8_t p = rb_->peek<uint8_t>(ReadBuffer::BIG);

    if(parsingObject && p == 0)
    {
        int32_t endMark = rb_->peek<int32_t>(ReadBuffer::BIG, 3);

        if(endMark == 0x0009)
        {
            return AMF0_ObjectEnd; 
        }
    }

    return (AMF0Types)p;
}

bool AMF0Parser::isFinished()
{
    return rb_->getUnReadSize() == 0;
}

void AMF0Parser::skip(AMF0Types t)
{
    switch(t)
    {
        case AMF0_Number:
            parseAsNumber();
            break;
        case AMF0_Boolean:
            parseAsBool();
            break;
        case AMF0_String:
            parseAsString();
            break;
        case AMF0_Object:
            skipObject();
            break;
        case AMF0_Null:
            parseNull();
            break;
        case AMF0_Undefined:
            parseUndefined();
            break;
        default:
            throw RtmpNotImplemented("AMF0Parser::skip()"); 
    }
}

void AMF0Parser::skipObject()
{
    skipObjectStart();

    AMF0Types t;
    bool parsingKey = true;
    while((t = getNextType(true)) != AMF0_ObjectEnd)
    {
        if(parsingKey)
        {
            parseObjectKey();
        }
        else
        {
            skip(t);
        }

        parsingKey = !parsingKey;
    }

    skipObjectEnd();
}

//-----------------------------------------------------------------
//                       AMF0Serializer                                          

AMF0Serializer::AMF0Serializer(WriteBuffer* wb):
    wb_(wb)
{
}

void AMF0Serializer::writeString(string v)
{
    wb_->writeB((uint8_t)AMF0_String);
    wb_->writeB((uint16_t)v.length());
    wb_->writeBytes((uint8_t*)v.c_str(), v.length());
}

void AMF0Serializer::writeNumber(double v)
{
    wb_->writeB((uint8_t)AMF0_Number);
    uint8_t* tmp = (uint8_t*)&v;
    Utility::reverseBytes(tmp, 8);
    wb_->writeBytes(tmp, 8);
}

void AMF0Serializer::writeObjectStart()
{
    wb_->writeB((uint8_t)AMF0_Object);
}

void AMF0Serializer::writeObjectEnd()
{
    wb_->writeB((uint16_t)0);
    wb_->writeB((uint8_t)AMF0_ObjectEnd);
}

void AMF0Serializer::writeObjectKey(string v)
{
    wb_->writeB((uint16_t)v.length());
    wb_->writeBytes((uint8_t*)v.c_str(), v.length());
}

void AMF0Serializer::writeBool(bool v)
{
    uint8_t b = (uint8_t)v;

    wb_->writeB((uint8_t)AMF0_Boolean);
    wb_->writeB(b);
}

void AMF0Serializer::writeNull()
{
    wb_->writeB((uint8_t)AMF0_Null);
}

void AMF0Serializer::writeUndefined()
{
    wb_->writeB((uint8_t)AMF0_Undefined);
}
