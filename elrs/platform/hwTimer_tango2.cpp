// ExpressLRS hwTimer on TIM5 (32-bit, APB1, own NVIC vector TIM5_IRQn = 50).
// TX only needs the "tock" callback at the packet interval. The counter runs at
// 1 MHz and the update interrupt fires every HWtimerInterval microseconds.
#include "hwTimer.h"
#include "stm32f4xx.h"
#include "tango2_platform.h"

void (*hwTimer::callbackTick)() = nullptr;
void (*hwTimer::callbackTock)() = nullptr;

volatile bool hwTimer::running = false;
volatile bool hwTimer::isTick = false;

volatile uint32_t hwTimer::HWtimerInterval = TimerIntervalUSDefault;
volatile int32_t hwTimer::PhaseShift = 0;
volatile int32_t hwTimer::FreqOffset = 0;

// TIM5 kernel clock: PCLK1, doubled when the APB1 prescaler is not 1.
static void (*volatile tockCallback)();
static volatile uint32_t intervalUs = TimerIntervalUSDefault;

// HCLK is 168 MHz under both hosts (monitor and EdgeTX).
static uint32_t tim5_clock_hz()
{
    const uint32_t hclk = 168000000u;
    uint32_t ppre1 = (RCC->CFGR & RCC_CFGR_PPRE1) >> RCC_CFGR_PPRE1_Pos;
    if (ppre1 < 4) return hclk;
    uint32_t div = 1u << (ppre1 - 3);
    return hclk / div * 2;
}

void tango2_timer_hw_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM5EN;
    (void)RCC->APB1ENR;
    TIM5->CR1 = 0;
    TIM5->DIER = 0;
    TIM5->PSC = tim5_clock_hz() / 1000000u - 1;
    TIM5->ARR = intervalUs - 1;
    TIM5->EGR = TIM_EGR_UG;   // load PSC/ARR
    TIM5->SR = 0;
    NVIC_SetPriority(TIM5_IRQn, TANGO2_IRQ_PRIORITY);
    NVIC_ClearPendingIRQ(TIM5_IRQn);
    NVIC_EnableIRQ(TIM5_IRQn);
}

void tango2_timer_hw_stop(void)
{
    TIM5->CR1 = 0;
    TIM5->DIER = 0;
    TIM5->SR = 0;
    NVIC_DisableIRQ(TIM5_IRQn);
    NVIC_ClearPendingIRQ(TIM5_IRQn);
    hwTimer::running = false;
}

void hwTimer::init(void (*callbackTick)(), void (*callbackTock)())
{
    hwTimer::callbackTick = callbackTick;
    hwTimer::callbackTock = callbackTock;
    tockCallback = callbackTock;
    tango2_timer_hw_init();
    running = false;
}

void hwTimer::stop()
{
    TIM5->CR1 &= ~TIM_CR1_CEN;
    TIM5->DIER = 0;
    TIM5->SR = 0;
    running = false;
}

void ICACHE_RAM_ATTR hwTimer::resume()
{
    if (running)
        return;
    isTick = false;
    // First tock one interval from now (the ESP8266 TX does the same).
    TIM5->ARR = HWtimerInterval - 1;
    TIM5->CNT = 0;
    TIM5->SR = 0;
    TIM5->DIER = TIM_DIER_UIE;
    running = true;
    TIM5->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
}

void hwTimer::updateInterval(uint32_t newTimerInterval)
{
    HWtimerInterval = newTimerInterval;
    intervalUs = newTimerInterval;
    TIM5->ARR = newTimerInterval - 1;   // ARPE: takes effect at the next update
}

void ICACHE_RAM_ATTR hwTimer::phaseShift(int32_t newPhaseShift)
{
    // RX-only feature; kept for the interface.
    int32_t lim = HWtimerInterval >> 2;
    PhaseShift = constrain(newPhaseShift, -lim, lim);
}

void ICACHE_RAM_ATTR hwTimer::callback()
{
    if (running && callbackTock)
        callbackTock();
}

void tango2_timer_isr(void)
{
    if (TIM5->SR & TIM_SR_UIF)
    {
        TIM5->SR = ~TIM_SR_UIF;
        if (hwTimer::running && tockCallback)
            tockCallback();
    }
}
