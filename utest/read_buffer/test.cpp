#include "../../readbuffer.h"
#include <stdio.h>

int main(int argc, char* argv[])
{
    ReadBuffer rb(1024);

    uint8_t data[]{0, 0, 2, 0};
    rb.appendData(data, 4);
    
    uint32_t value = rb.read<uint32_t>(ReadBuffer::BIG, 3);
    printf("%u\n", value);
}
