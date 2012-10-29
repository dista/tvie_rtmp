#ifndef LOG_H
#define LOG_H

#include <stdio.h>

#define LEVDEBUG 0
#define LEVINFO 1
#define LEVWARN 2
#define LEVERROR 3

#define DEFAULTLOGLEVEL LEVDEBUG

#define RTMP_LOG(logLevel, fmt, ...)     \
    do {                                 \
        if(logLevel >= DEFAULTLOGLEVEL)  \
        {                                \
            printf(fmt, ##__VA_ARGS__);  \
        }                                \
    } while(0)                           \

#endif
