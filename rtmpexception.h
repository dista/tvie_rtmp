#ifndef RTMP_EXCEPTION_H
#define RTMP_EXCEPTION_H

#include <exception>
#include <sstream>
#include <string>

using namespace std;

class RtmpException : public exception
{
private:
    int errorCode_;
    string errorMsg_;

public:
   RtmpException(const char* errorMsg, int errorCode): errorCode_(errorCode)
   {
       stringstream s;
       s << errorMsg << ". Error Code is: " << errorCode;

       errorMsg_ = s.str();
   }

   int getErrorCode()
   {
       return errorCode_;
   }

   virtual const char* what() const throw()
   {
       return errorMsg_.c_str();
   }

   virtual ~RtmpException() throw()
   {
   }
};

class RtmpInvalidArg: public RtmpException
{
public:
    RtmpInvalidArg(const char* errorMsg):
        RtmpException(errorMsg, -1)
    {

    }
};

class RtmpNoEnoughData: public RtmpException
{
    public:
        RtmpNoEnoughData():
            RtmpException("", -1)
       {
       }
};

class RtmpBadProtocalData: public RtmpException
{
    public:
        RtmpBadProtocalData(const char* errorMsg):
            RtmpException(errorMsg, -1)
    {
    }
};

class RtmpNotSupported: public RtmpException
{
    public:
        RtmpNotSupported(const char* errorMsg):
            RtmpException(errorMsg, -1)
    {
        
    }
        RtmpNotSupported(string errorMsg):
            RtmpException(errorMsg.c_str(), -1)
    {
    }
};

class RtmpBadState: public RtmpException
{
    public:
        RtmpBadState(const char* errorMsg):
            RtmpException(errorMsg, -1)
    {
    }
};

class RtmpInvalidAMFData: public RtmpException
{
    public:
        RtmpInvalidAMFData(const char* errorMsg):
            RtmpException(errorMsg, -1)
    {
    }
};

class RtmpInternalError: public RtmpException
{
    public:
        RtmpInternalError(const char* errorMsg):
            RtmpException(errorMsg, -1)
    {
    }
        RtmpInternalError(const char* errorMsg, int errorNum):
            RtmpException(errorMsg, errorNum)
    {
    }
};

class RtmpNotImplemented: public RtmpException
{
    public:
        RtmpNotImplemented(const char* errorMsg):
            RtmpException(errorMsg, -1)
    {
    }
};

class RtmpPartialData: public RtmpException
{
    public:
        RtmpPartialData(const char* errorMsg):
            RtmpException(errorMsg, -1)
    {
    }
};

class RtmpContinue: public RtmpException
{
    public:
        RtmpContinue():
            RtmpException("", -1)
    {
    }
};

#endif
