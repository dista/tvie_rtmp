#include "tviertmp.h"
#include <iostream>

#include "livereceiveractor.h"

extern "C"
{
#define __STDC_CONSTANT_MACROS
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}


using namespace std;

int main(int argc, char* argv[])
{
    //LiveReceiverActor::Init("http://10.33.0.56:10080/live", ".ismv");
    LiveReceiverActor::Init("http://10.33.0.56:10080/live", ".ismv");
    RtmpServer s(1935, LiveReceiverActor::createActor);
    s.start();

    return 0;
}
