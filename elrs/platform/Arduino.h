// Minimal Arduino API for building ExpressLRS as a Tango 2 image (no Arduino core).
// Time comes from the host (elrs_host_api_t), pins are Tango 2 GPIOs encoded as
// port * 16 + pin (PA0 = 0, PC13 = 45, PE10 = 74). See tango2_platform.cpp.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>

#ifdef __cplusplus
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdlib>
using std::max;
using std::min;
#endif

#define PIN_PA(n) (0 * 16 + (n))
#define PIN_PB(n) (1 * 16 + (n))
#define PIN_PC(n) (2 * 16 + (n))
#define PIN_PD(n) (3 * 16 + (n))
#define PIN_PE(n) (4 * 16 + (n))

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
#define RISING 1
#define FALLING 2
#define CHANGE 3

#define HEX 16
#define DEC 10

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#define PROGMEM
#define PSTR(s) (s)
#define WORD_ALIGNED_ATTR __attribute__((aligned(4)))

typedef bool boolean;
typedef uint8_t byte;

#ifdef __cplusplus
extern "C" {
#endif
unsigned long millis(void);
unsigned long micros(void);
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void pinMode(int pin, int mode);
void digitalWrite(int pin, int val);
int digitalRead(int pin);
void attachInterrupt(int irq, void (*fn)(void), int mode);
void detachInterrupt(int irq);
void noInterrupts(void);
void interrupts(void);
void yield(void);
#ifdef __cplusplus
}
#endif

#define digitalPinToInterrupt(p) (p)
#define bit(x) (1UL << (x))
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

#ifdef __cplusplus
static inline void dacWrite(int, int) {}
static inline char *strchrnul(const char *s, int c)
{
  while (*s && *s != (char)c) s++;
  return (char *)s;
}

static inline long map(long x, long in_min, long in_max, long out_min, long out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

class Print
{
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t *s, size_t l)
  {
    size_t n = 0;
    while (l--) n += write(*s++);
    return n;
  }
  size_t write(const char *s) { return write((const uint8_t *)s, strlen(s)); }
  size_t print(const char *s) { return write(s); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(int v, int base = DEC) { char b[16]; snprintf(b, sizeof(b), base == HEX ? "%x" : "%d", v); return print(b); }
  size_t print(unsigned v, int base = DEC) { char b[16]; snprintf(b, sizeof(b), base == HEX ? "%x" : "%u", v); return print(b); }
  size_t print(long v, int base = DEC) { return print((int)v, base); }
  size_t print(unsigned long v, int base = DEC) { return print((unsigned)v, base); }
  size_t print(uint8_t v, int base = DEC) { return print((unsigned)v, base); }
  size_t println() { return write((uint8_t)'\n'); }
  template <typename T> size_t println(T v) { size_t n = print(v); return n + println(); }
  template <typename T> size_t println(T v, int base) { size_t n = print(v, base); return n + println(); }
  int printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)))
  {
    char b[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    print(b);
    return n;
  }
};

class Stream : public Print
{
public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual void flush() {}
  using Print::write;
  virtual size_t readBytes(uint8_t *buffer, size_t length)
  {
    size_t count = 0;
    while (count < length && available() > 0)
    {
      int v = read();
      if (v < 0) break;
      buffer[count++] = (uint8_t)v;
    }
    return count;
  }
  size_t readBytes(char *buffer, size_t length) { return readBytes((uint8_t *)buffer, length); }
  void setTimeout(unsigned long) {}
};

class EspClass
{
public:
  void restart();
};
extern EspClass ESP;
#endif
