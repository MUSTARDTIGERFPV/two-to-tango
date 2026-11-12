// Arduino-style platform layer for the Tango 2 ELRS image: time from the host,
// the handful of GPIOs the image owns, the DIO0 EXTI, interrupt masking,
// logging into a ring that loop() drains to host->log.
#include "Arduino.h"
#include "stm32f4xx.h"
#include "tango2_platform.h"

const elrs_host_api_t *host;

EspClass ESP;

unsigned long millis(void) { return host->millis(); }
unsigned long micros(void) { return host->micros(); }
void delay(unsigned long ms) { host->delay_us(ms * 1000); }
void delayMicroseconds(unsigned int us) { host->delay_us(us); }
#if defined(TANGO2_NATIVE_SIM)
void sim_yield(void);
void yield(void) { sim_yield(); }   // lets the PC simulator deliver "interrupts" during busy-waits
#else
void yield(void) {}
#endif

void EspClass::restart()
{
  // The image can't reset the radio; EdgeTX decides. Just say so.
  if (host) host->log("ELRS: restart requested (ignored)");
}

// ---- interrupt masking (nestable; safe from ISRs) ----
static volatile uint32_t irq_nest;
static volatile uint32_t irq_saved;

void noInterrupts(void)
{
  uint32_t p = __get_PRIMASK();
  __disable_irq();
  if (irq_nest++ == 0) irq_saved = p;
}

void interrupts(void)
{
  if (irq_nest == 0) return;
  if (--irq_nest == 0 && !irq_saved) __enable_irq();
}

// ---- GPIO: only the pins the image owns ----
static GPIO_TypeDef *port_of(int pin)
{
  static GPIO_TypeDef *const ports[] = { GPIOA, GPIOB, GPIOC, GPIOD, GPIOE };
  return ports[pin >> 4];
}

static bool owned(int pin)
{
  return pin == TANGO2_PIN_RFSW || pin == TANGO2_PIN_RESET || pin == TANGO2_PIN_PA2 ||
         (pin >= TANGO2_PIN_DIO0 && pin <= TANGO2_PIN_DIO0 + 5);
}

void pinMode(int pin, int mode)
{
  if (pin < 0 || !owned(pin)) return;
  GPIO_TypeDef *p = port_of(pin);
  int n = pin & 15;
  uint32_t m = (mode == OUTPUT) ? 1u : 0u;
  uint32_t pull = (mode == INPUT_PULLUP) ? 1u : (mode == INPUT_PULLDOWN) ? 2u : 0u;
  p->PUPDR = (p->PUPDR & ~(3u << (2 * n))) | (pull << (2 * n));
  p->MODER = (p->MODER & ~(3u << (2 * n))) | (m << (2 * n));
}

void digitalWrite(int pin, int val)
{
  if (pin < 0 || !owned(pin)) return;
  port_of(pin)->BSRR = val ? (1u << (pin & 15)) : (1u << ((pin & 15) + 16));
}

int digitalRead(int pin)
{
  if (pin < 0) return 0;
  return (port_of(pin)->IDR >> (pin & 15)) & 1;
}

// ---- DIO0 interrupt: PE10 on EXTI10 (shared EXTI15_10 vector) ----
static void (*volatile dio_handler)(void);

void attachInterrupt(int pin, void (*fn)(void), int mode)
{
  if (pin != TANGO2_PIN_DIO0) return;
  int line = pin & 15;
  RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
  (void)RCC->APB2ENR;
  // EXTICR3 covers lines 8..11; port E = 4
  SYSCFG->EXTICR[line >> 2] = (SYSCFG->EXTICR[line >> 2] & ~(0xFu << (4 * (line & 3)))) | (4u << (4 * (line & 3)));
  dio_handler = fn;
  if (mode == RISING || mode == CHANGE) EXTI->RTSR |= 1u << line; else EXTI->RTSR &= ~(1u << line);
  if (mode == FALLING || mode == CHANGE) EXTI->FTSR |= 1u << line; else EXTI->FTSR &= ~(1u << line);
  EXTI->PR = 1u << line;
  EXTI->IMR |= 1u << line;
  NVIC_SetPriority(EXTI15_10_IRQn, TANGO2_IRQ_PRIORITY);
  NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void detachInterrupt(int pin)
{
  if (pin != TANGO2_PIN_DIO0) return;
  EXTI->IMR &= ~(1u << (pin & 15));
  dio_handler = nullptr;
}

void tango2_exti_disable(void)
{
  EXTI->IMR &= ~(0x3Fu << 10);
  EXTI->PR = 0x3Fu << 10;
  dio_handler = nullptr;
}

void tango2_exti_isr(void)
{
  uint32_t pr = EXTI->PR & (0x3Fu << 10);
  EXTI->PR = pr;
  if ((pr & (1u << 10)) && dio_handler) dio_handler();
}

// ---- RF switch / PA2 ----
void tango2_pa2_for_power(uint8_t paConfig)
{
  // TBS: PA2 high for its two lowest levels (~1 dBm), low above. PA_BOOST
  // drive = 2 + OutputPower dBm, so anything above 5 dBm drive is "low".
  digitalWrite(TANGO2_PIN_PA2, (paConfig & 0x0F) > 3 ? LOW : HIGH);
}

void tango2_rf_safe(void)
{
  digitalWrite(TANGO2_PIN_RFSW, HIGH);
  digitalWrite(TANGO2_PIN_PA2, HIGH);
}

// ---- logging: ELRS writes text into a ring from any context; loop() drains whole lines ----
static char log_ring[1024];
static volatile uint16_t log_head, log_tail;

class LogStream : public Stream
{
public:
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  size_t write(uint8_t c) override
  {
    if (c == '\r') return 1;
    uint16_t next = (log_head + 1) % sizeof(log_ring);
    if (next != log_tail)
    {
      log_ring[log_head] = (char)c;
      log_head = next;
    }
    return 1;
  }
  size_t write(const uint8_t *s, size_t l) override
  {
    for (size_t i = 0; i < l; i++) write(s[i]);
    return l;
  }
};

static LogStream logStream;
Stream *tango2_log_stream = &logStream;

void tango2_log_drain(void)
{
  char line[61];  // host log frames carry at most 63 bytes; one call per line, no newline
  for (;;)
  {
    uint16_t t = log_tail, n = 0;
    bool full = false;
    while (t != log_head)
    {
      char c = log_ring[t];
      t = (t + 1) % sizeof(log_ring);
      if (c == '\n') { full = true; break; }
      if (n < sizeof(line) - 1) line[n++] = c;
    }
    if (!full) return;
    line[n] = 0;
    log_tail = t;
    host->log("%s", line);
  }
}
