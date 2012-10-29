#include "utility.h"
#include <sys/time.h>
#include <stdio.h>
#include <iostream>
#include <sstream>

uint32_t Utility::getTimestamp()
{
    timeval tv;

    gettimeofday(&tv, NULL);

    return tv.tv_sec;
}

bool Utility::compareData(uint8_t* data1, uint8_t* data2, int size)
{
    for(int i = 0; i < size; i++)
    {
        if(data1[i] != data2[i])
        {
            return false;
        }
    }

    return true;
}

void Utility::reverseBytes(uint8_t* bytes, int size)
{
    for(int i = 0; i < size / 2; i++)
    {
        uint8_t tmp = bytes[i];
        bytes[i] = bytes[size - i - 1];
        bytes[size - i - 1] = tmp;
    }
}

bool Utility::dumpData(uint8_t* bytes, int size, char* fileName)
{
    FILE* fp;
    fp = fopen(fileName, "wb");

    if(!fp)
    {
        return false;
    }

    size_t s = fwrite(bytes, 1, size, fp);

    if(s != size)
    {
        return false;
    }

    fclose(fp);
    return true;
}

std::string Utility::numToStr(double num)
{
    std::ostringstream converter;

    converter << num;

    return converter.str();
}
