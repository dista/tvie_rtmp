#include "../../writebuffer.h"

int main(int argc, char* argv[])
{
    WriteBuffer wb(20);

    wb.writeB(16);
    wb.writeB((uint16_t)16);
}
