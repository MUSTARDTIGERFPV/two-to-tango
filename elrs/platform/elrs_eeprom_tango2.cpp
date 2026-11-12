// ELRS_EEPROM backed by the host's config blob (SD card under EdgeTX, RAM
// under the loader). Begin() loads it, Commit() saves the whole buffer.
#include "elrs_eeprom.h"
#include "targets.h"
#include "logging.h"
#include "tango2_platform.h"

static uint8_t eeprom_buf[RESERVED_EEPROM_SIZE];
static bool eeprom_loaded;

void ELRS_EEPROM::Begin()
{
    if (eeprom_loaded)
        return;
    memset(eeprom_buf, 0, sizeof(eeprom_buf));   // version 0 -> ELRS writes defaults
    int n = host->config_load(eeprom_buf, sizeof(eeprom_buf));
    if (n < 0)
        memset(eeprom_buf, 0, sizeof(eeprom_buf));
    eeprom_loaded = true;
}

uint8_t ELRS_EEPROM::ReadByte(const uint32_t address)
{
    Begin();   // TxConfig::Load() reads without calling Begin() on non-ESP32
    if (address >= RESERVED_EEPROM_SIZE)
        return 0;
    return eeprom_buf[address];
}

void ELRS_EEPROM::WriteByte(const uint32_t address, const uint8_t value)
{
    if (address >= RESERVED_EEPROM_SIZE)
        return;
    eeprom_buf[address] = value;
}

uint8_t *tango2_eeprom_buffer()
{
    ELRS_EEPROM().Begin();
    return eeprom_buf;
}

void ELRS_EEPROM::Commit()
{
    Begin();
    if (host->config_save(eeprom_buf, sizeof(eeprom_buf)) < 0)
        ERRLN("config save failed");
}
