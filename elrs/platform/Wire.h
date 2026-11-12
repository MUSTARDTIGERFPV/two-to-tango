#pragma once
// No I2C in the Tango 2 image; rxtx_common.cpp only calls this when SCL/SDA pins exist.
class TwoWire
{
public:
    void begin(int, int) {}
    void setClock(unsigned long) {}
};
extern TwoWire Wire;
