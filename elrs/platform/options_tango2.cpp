// firmwareOptions for the Tango 2 image. No options JSON / LittleFS here:
// the UID comes from the build (MY_UID, from the bind phrase, computed by
// build.py exactly like ExpressLRS's python/build_flags.py) and can be
// overridden at runtime by a UID record at the end of the config blob.
#include "options.h"
#include "logging.h"
#include "elrs_eeprom.h"
#include "tango2_platform.h"

#define QUOTE(arg) #arg
#define STR(macro) QUOTE(macro)

const unsigned char target_name[] = "\xBE\xEF\xCA\xFE" "TANGO2_ELRS_TX";
const uint8_t target_name_size = sizeof(target_name);
const char commit[] {LATEST_COMMIT, 0};
const char version[] = {LATEST_VERSION, 0};

const char *wifi_hostname = "elrs_tx";
const char *wifi_ap_ssid = "ExpressLRS TX";
const char *wifi_ap_password = "expresslrs";
const char *wifi_ap_address = "10.0.0.1";

char device_name[ELRSOPTS_DEVICENAME_SIZE + 1] = DEVICE_NAME;
char product_name[ELRSOPTS_PRODUCTNAME_SIZE + 1] = "TBS Tango 2 internal (SX1272)";
firmware_options_t firmwareOptions;

#ifndef TANGO2_DOMAIN
#define TANGO2_DOMAIN 1   // index into FHSS domains[]: 1 = FCC915
#endif

// UID record in the config blob (after tx_config_t)
static constexpr uint32_t UID_RECORD_OFFSET = RESERVED_EEPROM_SIZE - 12;
static constexpr uint8_t UID_RECORD_MAGIC[4] = {'U', 'I', 'D', '1'};
extern uint8_t *tango2_eeprom_buffer();

// The UID as flashed. A web flasher sets it without a rebuild: it finds the
// magic in the image, writes has_uid and uid, and recomputes the header CRC
// (abi/elrs_image.h). volatile so the compiler reads it from flash.
struct flashed_uid_t
{
    char magic[8];
    uint8_t has_uid;
    uint8_t uid[6];
};
extern "C" __attribute__((used)) const volatile flashed_uid_t tango2_flashed_uid = {
    "T2UID01",
#if defined(MY_UID)
    1, {MY_UID},
#else
    0, {0},
#endif
};

bool options_init()
{
    memset(&firmwareOptions, 0, sizeof(firmwareOptions));
    firmwareOptions.domain = TANGO2_DOMAIN;
    if (tango2_flashed_uid.has_uid)
    {
        for (int i = 0; i < 6; i++)
            firmwareOptions.uid[i] = tango2_flashed_uid.uid[i];
        firmwareOptions.hasUID = true;
    }
    const uint8_t *rec = tango2_eeprom_buffer() + UID_RECORD_OFFSET;
    if (memcmp(rec, UID_RECORD_MAGIC, 4) == 0)
    {
        firmwareOptions.hasUID = rec[4];
        memcpy(firmwareOptions.uid, rec + 5, 6);
    }
    firmwareOptions.wifi_auto_on_interval = -1;
    firmwareOptions.tlm_report_interval = 240U;
    firmwareOptions.fan_min_runtime = 30U;
    firmwareOptions.unlock_higher_power = false;
    firmwareOptions.is_airport = false;
    firmwareOptions.uart_baud = 460800;
    return true;
}

void saveOptions()
{
    uint8_t *rec = tango2_eeprom_buffer() + UID_RECORD_OFFSET;
    memcpy(rec, UID_RECORD_MAGIC, 4);
    rec[4] = firmwareOptions.hasUID;
    memcpy(rec + 5, firmwareOptions.uid, 6);
    ELRS_EEPROM().Commit();
}
