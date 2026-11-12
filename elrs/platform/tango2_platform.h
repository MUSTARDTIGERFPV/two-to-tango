// Tango 2 ELRS image: platform glue shared by the platform sources.
#pragma once
#include <stdint.h>
#include "elrs_image.h"

extern const elrs_host_api_t *host;

// Pins the image owns (the host owns the rest); pinMode/digitalWrite
// ignore everything else, including PC1 (NSS is driven by host->spi_xfer).
#define TANGO2_PIN_RFSW  (2 * 16 + 0)    // PC0: low = TX, high = RX
#define TANGO2_PIN_RESET (2 * 16 + 13)   // PC13: SX1272 reset, active high
#define TANGO2_PIN_PA2   (0 * 16 + 2)    // PA2: TBS "PA enable", low = on
#define TANGO2_PIN_DIO0  (4 * 16 + 10)   // PE10 .. PE15 = DIO0 .. DIO5

#define TANGO2_IRQ_PRIORITY 4            // NVIC priority for the DIO EXTI and the hwTimer (same level)

void tango2_log_drain(void);             // move buffered log text to host->log (loop context)
void tango2_exti_isr(void);              // EXTI15_10 handler body
void tango2_timer_isr(void);             // hwTimer handler body
void tango2_timer_hw_init(void);         // clock, NVIC; timer stopped
void tango2_timer_hw_stop(void);
void tango2_exti_disable(void);
void tango2_pa2_for_power(uint8_t paConfig);  // TBS rule, called when PaConfig is committed
void tango2_rf_safe(void);               // RF switch to RX, PA2 high
