// Entry points of the Tango 2 ELRS image (abi/elrs_image.h): the header at
// 0x080E8000 and init/start/loop/stop around ExpressLRS's setup()/loop().
#include "targets.h"
#include "common.h"
#include "FHSS.h"
#include "hwTimer.h"
#include "logging.h"
#include "stm32f4xx.h"
#include "tango2_platform.h"

extern "C" {
extern char __data_start[], __data_end[], __data_load[], __bss_start[], __bss_end[];
extern void (*__preinit_array_start[])(void), (*__preinit_array_end[])(void);
extern void (*__init_array_start[])(void), (*__init_array_end[])(void);
void tango2_heap_reset(void);
}

// ExpressLRS tx_main.cpp
void setup();
void loop();
extern Stream *TxUSB;
extern Stream *tango2_log_stream;
extern SX127xDriver Radio;

extern const char version[];
static bool running;

// FHSS frequencies are "start + spread*ch - FreqCorrection" in FRF units. The TX
// never touches FreqCorrection otherwise (it's the RX's AFC term), so it carries
// the per-unit crystal error: program f/(1+e) ~= f - f*e for a crystal off by e.
void tango2_apply_crystal_correction()
{
    int32_t ppm_x10 = host->crystal_ppm_x10();
    int64_t center_reg = (int64_t)((double)FHSSconfig->freq_center / FREQ_STEP);
    FreqCorrection = (int32_t)((center_reg * ppm_x10 + (ppm_x10 >= 0 ? 5000000 : -5000000)) / 10000000);
    DBGLN("crystal %d ppm/10 -> FreqCorrection %d", ppm_x10, FreqCorrection);
}

// STM32 96-bit unique ID folded into 6 bytes: the UID when no bind phrase is set.
void tango2_get_uid(uint8_t uid[6])
{
    const uint8_t *u = (const uint8_t *)0x1FFF7A10;
    for (int i = 0; i < 6; i++)
        uid[i] = u[i] ^ u[i + 6];
}

static int img_init(const elrs_host_api_t *h)
{
#if !defined(TANGO2_NATIVE_SIM)   // the PC simulator has a normal C runtime
    memcpy(__data_start, __data_load, __data_end - __data_start);
    memset(__bss_start, 0, __bss_end - __bss_start);
    tango2_heap_reset();
    for (auto f = __preinit_array_start; f < __preinit_array_end; f++) (*f)();
    for (auto f = __init_array_start; f < __init_array_end; f++) (*f)();
#endif
    host = h;
    if (h->abi_major != ELRS_ABI_MAJOR)
        return -1;

    BackpackOrLogStrm = tango2_log_stream;
    TxUSB = tango2_log_stream;

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIOEEN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;
    // Safe state for the pins we own: RF switch RX, PA2 high, reset released, DIOs input
    digitalWrite(TANGO2_PIN_RFSW, HIGH);
    pinMode(TANGO2_PIN_RFSW, OUTPUT);
    digitalWrite(TANGO2_PIN_PA2, HIGH);
    pinMode(TANGO2_PIN_PA2, OUTPUT);
    digitalWrite(TANGO2_PIN_RESET, LOW);
    pinMode(TANGO2_PIN_RESET, OUTPUT);
    for (int i = 0; i < 6; i++)
        pinMode(TANGO2_PIN_DIO0 + i, INPUT);
    tango2_exti_disable();

    return 0;
}

// setup() needs delays and the radio, so it runs here with interrupts on.
static void img_start(void)
{
    host->log("ELRS %s: starting", version);
    setup();
    running = true;
    tango2_log_drain();
    if (connectionState == radioFailed)
        host->log("ELRS: SX1272 not found");
    else if (connectionState == hardwareUndefined)
        host->log("ELRS: hardware undefined");
    else
        host->log("ELRS: ready, waiting for handset CRSF");
}

static void img_loop(void)
{
    if (!running)
        return;
    loop();
    tango2_log_drain();
}

static void img_stop(void)
{
    running = false;
    hwTimer::stop();
    tango2_timer_hw_stop();
    tango2_exti_disable();
    NVIC_DisableIRQ(EXTI15_10_IRQn);
    if (connectionState != radioFailed && connectionState != hardwareUndefined)
        Radio.SetTxIdleMode();   // standby
    tango2_rf_safe();
    tango2_log_drain();
    host->log("ELRS: stopped");
}

extern "C" {
int tango2_init(const elrs_host_api_t *h) { return img_init(h); }
void tango2_start(void) { img_start(); }
void tango2_loop(void) { img_loop(); }
void tango2_stop(void) { img_stop(); }
void tango2_irq_exti(void) { tango2_exti_isr(); }
void tango2_irq_timer(void) { tango2_timer_isr(); }
}
