// Devices and hooks the Tango 2 image doesn't have: LED/RGB (EdgeTX owns the
// status LED), buttons, ADC, WiFi/web update, backpack, MAVLink conversion.
#include "targets.h"
#include "common.h"
#include "device.h"
#include "devButton.h"
#include "devADC.h"
#include "CRSFHandset.h"
#include "crsf_protocol.h"

device_t LED_device = {nullptr, nullptr, nullptr, nullptr, EVENT_NONE};
device_t RGB_device = {nullptr, nullptr, nullptr, nullptr, EVENT_NONE};
device_t ADC_device = {nullptr, nullptr, nullptr, nullptr, EVENT_NONE};
device_t WIFI_device = {nullptr, nullptr, nullptr, nullptr, EVENT_NONE};
device_t Button_device = {nullptr, nullptr, nullptr, nullptr, EVENT_NONE};

int getADCReading(adc_reading) { return 0; }
void registerButtonFunction(action_e, ButtonAction_fn) {}
size_t button_GetActionCnt() { return 0; }

bool webserverPreventAutoStart;
bool BackpackTelemReadyToSend;
bool TxBackpackWiFiReadyToSend;
bool VRxBackpackWiFiReadyToSend;

// "Enable WiFi" on the TX: there is no WiFi in the image.
void setWifiUpdateMode() {}

// MAVLink link mode: no MAVLink parser in the image (CRSF only).
void convert_mavlink_to_crsf_telem(crsf_addr_e, uint8_t *, uint8_t) {}
bool isThisAMavPacket(uint8_t *, uint16_t) { return false; }
uint16_t buildMAVLinkELRSModeChange(uint8_t, uint8_t *) { return 0; }
