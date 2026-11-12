// SPIEx replacement for the Tango 2 image: every transfer goes through the
// host's shared SPI2 layer (host->spi_xfer), which drives NSS (PC1) and keeps
// the transfer atomic with respect to the gimbal sensors.
#pragma once
#include "targets.h"
#include "tango2_platform.h"

class SPIExClass
{
public:
    void begin() {}
    void end() {}
    void ICACHE_RAM_ATTR read(uint8_t cs_mask, uint8_t *data, uint32_t size)
    {
        (void)cs_mask;
        host->spi_xfer(ELRS_SPI_SX1272, data, data, (uint16_t)size);
    }
    void ICACHE_RAM_ATTR write(uint8_t cs_mask, uint8_t *data, uint32_t size)
    {
        (void)cs_mask;
        host->spi_xfer(ELRS_SPI_SX1272, data, nullptr, (uint16_t)size);
    }
};

extern SPIExClass SPIEx;
