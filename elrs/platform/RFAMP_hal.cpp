// Tango 2 RF front end (replaces lib/RFAMP/RFAMP_hal.cpp): PC0 is a single
// TX/RX switch line, low = TX (verified: 43 dB). PA2 is set from the power
// level (tango2_pa2_for_power), not per packet.
#include "RFAMP_hal.h"
#include "tango2_platform.h"

RFAMP_hal *RFAMP_hal::instance = nullptr;

RFAMP_hal::RFAMP_hal() { instance = this; }

void RFAMP_hal::init()
{
    pinMode(TANGO2_PIN_RFSW, OUTPUT);
    digitalWrite(TANGO2_PIN_RFSW, HIGH);
    pinMode(TANGO2_PIN_PA2, OUTPUT);
    digitalWrite(TANGO2_PIN_PA2, HIGH);
    rx_enabled = false;
    tx_enabled = false;
}

void ICACHE_RAM_ATTR RFAMP_hal::TXenable(SX12XX_Radio_Number_t radioNumber)
{
    (void)radioNumber;
    digitalWrite(TANGO2_PIN_RFSW, LOW);
    tx_enabled = true;
    rx_enabled = false;
}

void ICACHE_RAM_ATTR RFAMP_hal::RXenable()
{
    digitalWrite(TANGO2_PIN_RFSW, HIGH);
    rx_enabled = true;
    tx_enabled = false;
}

void ICACHE_RAM_ATTR RFAMP_hal::TXRXdisable()
{
    digitalWrite(TANGO2_PIN_RFSW, HIGH);   // TBS: everything but TX -> high
    rx_enabled = false;
    tx_enabled = false;
}
