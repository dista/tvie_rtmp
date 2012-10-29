#ifndef UTILITY_H
#define UTILITY_H

#include <stdint.h>
#include <string>

class Utility
{
public:
    static uint32_t getTimestamp();
    static bool compareData(uint8_t* data1, uint8_t* data2, int size);
    static void reverseBytes(uint8_t* bytes, int size);
    static bool dumpData(uint8_t* bytes, int size, char* fileName);
    static std::string numToStr(double num);
};

#endif
