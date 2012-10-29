#ifndef AMF0_H
#define AMF0_H

#include "readbuffer.h"
#include "writebuffer.h"
#include <boost/shared_ptr.hpp>

enum AMF0Types
{
    AMF0_Number = 0,
    AMF0_Boolean,
    AMF0_String,
    AMF0_Object,
    AMF0_Movieclip,
    AMF0_Null,
    AMF0_Undefined,
    AMF0_Reference,
    AMF0_EcmaArray,
    AMF0_ObjectEnd,
    AMF0_StrictArray,
    AMF0_Date,
    AMF0_LongString,
    AMF0_Unsupported,
    AMF0_Recordset,
    AMF0_XmlDocument,
    AMF0_TypedObjectMake
};

class AMF0Parser
{
    private:
        ReadBufferPtr rb_;
    public:
        AMF0Parser(ReadBufferPtr& ptr);
        string parseAsString();
        string parseObjectKey();
        bool parseAsBool();
        double parseAsNumber();
        void skipEcmaArrayStart();
        void skipObjectStart();
        void skipObjectEnd();
        AMF0Types getNextType(bool parsingObject);
        bool isFinished();
        void parseNull();
        void parseUndefined();
        void skipObject();
        void skipEcmaArray();
        void skip(AMF0Types t);
};

typedef boost::shared_ptr<AMF0Parser> AMF0ParserPtr;

class AMF0Serializer
{
    private:
        WriteBuffer* wb_;
    public:
        AMF0Serializer(WriteBuffer* wb);
        void writeString(string v);
        void writeNumber(double v);

        void writeObjectStart();
        void writeObjectEnd();
        void writeObjectKey(string v);
        void writeBool(bool v);
        void writeNull();
        void writeUndefined();
};

typedef boost::shared_ptr<AMF0Serializer> AMF0SerializerPtr;

#endif
