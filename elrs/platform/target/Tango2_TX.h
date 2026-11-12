// ExpressLRS target definition: TBS Tango 2 internal SX1272, running as an image
// next to EdgeTX (see abi/elrs_image.h). Pin numbers are port * 16 + pin.
// Force-included before every source file (-include target/Tango2_TX.h).
#pragma once

#define PLATFORM_TANGO2 1
#define DEVICE_NAME "Tango2 ELRS"

#ifdef __cplusplus
#include <stdint.h>
// PA_BOOST OutputPower (drive = 2 + value dBm) for 50/100/250/500 mW. Conducted,
// 915 MHz, fresh 1S cell, measured: drive 3/5/9/13 dBm ->
// 17.1/19.5/23.8/27.1 dBm (51/90/239/507 mW) after the external PA (~14 dB).
static const int16_t tango2_power_values[] = {1, 3, 7, 11};
#endif

// Serial: the handset link is the host's CRSF frame queue, not a UART
#define GPIO_PIN_RCSIGNAL_RX UNDEF_PIN
#define GPIO_PIN_RCSIGNAL_TX UNDEF_PIN

// Radio (SPI2 itself and the NSS pin PC1 belong to the host: host->spi_xfer)
#define GPIO_PIN_BUSY UNDEF_PIN
#define GPIO_PIN_BUSY_2 UNDEF_PIN
#define GPIO_PIN_DIO0 (4 * 16 + 10)   // PE10, EXTI10 (confirmed TxDone)
#define GPIO_PIN_DIO0_2 UNDEF_PIN
#define GPIO_PIN_DIO1 UNDEF_PIN
#define GPIO_PIN_DIO1_2 UNDEF_PIN
#define GPIO_PIN_MISO UNDEF_PIN
#define GPIO_PIN_MOSI UNDEF_PIN
#define GPIO_PIN_NSS (2 * 16 + 1)     // PC1, informational only
#define GPIO_PIN_NSS_2 UNDEF_PIN
#define GPIO_PIN_RST (2 * 16 + 13)    // PC13, active HIGH on the SX1272
#define GPIO_PIN_RST_2 UNDEF_PIN
#define GPIO_PIN_SCK (1 * 16 + 10)    // PB10, only tested against UNDEF_PIN
#define OPT_USE_HARDWARE_DCDC false
#define OPT_USE_SX1276_RFO_HF false

// Radio antenna
#define GPIO_PIN_ANT_CTRL UNDEF_PIN

// Radio power / RF switch: handled by platform/RFAMP_hal.cpp (PC0 low = TX, PA2 per TBS rule)
#define GPIO_PIN_PA_ENABLE UNDEF_PIN
#define GPIO_PIN_RFamp_APC2 UNDEF_PIN
#define GPIO_PIN_RX_ENABLE UNDEF_PIN
#define GPIO_PIN_TX_ENABLE UNDEF_PIN
#define GPIO_PIN_RX_ENABLE_2 UNDEF_PIN
#define GPIO_PIN_TX_ENABLE_2 UNDEF_PIN
#define GPIO_PIN_PA_PDET UNDEF_PIN
#define LBT_RSSI_THRESHOLD_OFFSET_DB 0

// Power: SX1272 PA_BOOST into an external PA that is always in circuit (PA2
// changes nothing on this unit, RFO is not connected). The minimum is ~40 mW
// (drive clamped at 2 dBm), so no 10/25 mW. The PA saturates near 29.8 dBm and
// the last dB needs the SX1272's +20 dBm mode (1% duty rating), so no 1 W.
#define MinPower PWR_50mW
#define MaxPower PWR_500mW
#define DefaultPower PWR_50mW
#define POWER_OUTPUT_DACWRITE false
#define POWER_OUTPUT_VALUES tango2_power_values
#define POWER_OUTPUT_VALUES_COUNT ((int)(sizeof(tango2_power_values) / sizeof(tango2_power_values[0])))
#define POWER_OUTPUT_VALUES2 ((const int16_t *)nullptr)
#define POWER_OUTPUT_VALUES2_COUNT 0
#define POWER_OUTPUT_VALUES_DUAL ((const int16_t *)nullptr)
#define POWER_OUTPUT_VALUES_DUAL_COUNT 0
#define SKY85321_PDET_INTERCEPT 0
#define SKY85321_PDET_SLOPE 0

// Input / UI: none (EdgeTX owns keys, LCD and the status LED)
#define GPIO_PIN_BUTTON UNDEF_PIN
#define GPIO_PIN_BUTTON2 UNDEF_PIN
#define GPIO_PIN_JOYSTICK UNDEF_PIN
#define GPIO_PIN_FIVE_WAY_INPUT1 UNDEF_PIN
#define GPIO_PIN_FIVE_WAY_INPUT2 UNDEF_PIN
#define GPIO_PIN_FIVE_WAY_INPUT3 UNDEF_PIN
#define JOY_ADC_VALUES nullptr
#define USER_BUTTON_LED UNDEF_PIN
#define USER_BUTTON2_LED UNDEF_PIN
#define GPIO_PIN_LED_BLUE UNDEF_PIN
#define GPIO_LED_BLUE_INVERTED false
#define GPIO_PIN_LED_GREEN UNDEF_PIN
#define GPIO_LED_GREEN_INVERTED false
#define GPIO_PIN_LED_RED UNDEF_PIN
#define GPIO_LED_RED_INVERTED false
#define GPIO_PIN_LED_WS2812 UNDEF_PIN
#define OPT_WS2812_IS_GRB false
#define WS2812_STATUS_LEDS nullptr
#define WS2812_STATUS_LEDS_COUNT 0
#define WS2812_VTX_STATUS_LEDS nullptr
#define WS2812_VTX_STATUS_LEDS_COUNT 0
#define WS2812_BOOT_LEDS nullptr
#define WS2812_BOOT_LEDS_COUNT 0

// Not present
#define GPIO_PIN_SCL UNDEF_PIN
#define GPIO_PIN_SDA UNDEF_PIN
#define OPT_HAS_THERMAL false
#define OPT_HAS_THERMAL_LM75A false
#define GPIO_PIN_FAN_EN UNDEF_PIN
#define GPIO_PIN_FAN_PWM UNDEF_PIN
#define GPIO_PIN_FAN_TACHO UNDEF_PIN
#define GPIO_PIN_FAN_SPEEDS nullptr
#define GPIO_PIN_FAN_SPEEDS_COUNT 0
#define OPT_USE_TX_BACKPACK false
#define GPIO_PIN_BACKPACK_EN UNDEF_PIN
#define GPIO_PIN_BACKPACK_BOOT UNDEF_PIN
#define BACKPACK_LOGGING_BAUD 0
#define PASSTHROUGH_BAUD 0
#define GPIO_PIN_DEBUG_RX UNDEF_PIN
#define GPIO_PIN_DEBUG_TX UNDEF_PIN
#define OPT_HAS_GSENSOR false
#define OPT_HAS_GSENSOR_STK8xxx false
#define GPIO_PIN_GSENSOR_INT UNDEF_PIN
#define OPT_HAS_SCREEN false
#define OPT_HAS_OLED_I2C false
#define OPT_HAS_OLED_SPI false
#define OPT_HAS_TFT_SCREEN false
#define OPT_SCREEN_MIRROR false
#define OPT_SCREEN_REVERSED false
#define GPIO_PIN_SCREEN_BL UNDEF_PIN
#define GPIO_PIN_SCREEN_CS UNDEF_PIN
#define GPIO_PIN_SCREEN_DC UNDEF_PIN
#define GPIO_PIN_SCREEN_MOSI UNDEF_PIN
#define GPIO_PIN_SCREEN_RST UNDEF_PIN
#define GPIO_PIN_SCREEN_SCK UNDEF_PIN
#define GPIO_PIN_SCREEN_SDA UNDEF_PIN
#define OPT_HAS_VTX_SPI false
#define GPIO_PIN_SPI_VTX_NSS UNDEF_PIN
