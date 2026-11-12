// Development host for the ELRS image (abi/elrs_image.h): the same services
// EdgeTX will provide, with the USB link standing in for the handset.
//
// After 'G' the USB stream is framed both ways:
//   PC -> radio: CRSF frames [sync][len][type][payload...][crc8 0xD5 over type..payload],
//                sync any of 0xC8 0xEA 0xEE 0xEC; handed to the image as-is.
//                Control: [0x7E][len][type][...][crc8] with type 0x01 = stop the image.
//   radio -> PC: CRSF frames from the image as-is, plus
//                [0x7E][len][0x7F][text...][crc8]  image log line
//                [0x7E][len][0x7D][text...][crc8]  host status line
// "OK" (text) follows once the image has stopped and command mode is back.
#include <Arduino.h>
#include <stdarg.h>
#include "elrs_image.h"

#define FRAME_MAX 64
#define RXQ_LEN 16

static uint8_t rxq[RXQ_LEN][FRAME_MAX];
static uint8_t rxq_len[RXQ_LEN];
static uint8_t rxq_head, rxq_tail;
static uint8_t cfg_blob[2048];
static uint32_t cfg_len;  // 0 = nothing saved yet (RAM only, lost on reset)

static uint32_t ram_vtor[128] __attribute__((aligned(512)));

static uint8_t crc8_d5(const uint8_t *d, unsigned n)
{
  uint8_t c = 0;
  while (n--) {
    c ^= *d++;
    for (int i = 0; i < 8; i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0xD5) : (uint8_t)(c << 1);
  }
  return c;
}

static uint32_t crc32(const uint8_t *p, uint32_t n)
{
  uint32_t crc = 0xFFFFFFFFu;
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
  }
  return crc ^ 0xFFFFFFFFu;
}

static void send_ctrl(uint8_t type, const char *text, unsigned n)
{
  uint8_t f[FRAME_MAX + 4];
  if (n > FRAME_MAX - 2) n = FRAME_MAX - 2;
  f[0] = 0x7E;
  f[1] = n + 2;
  f[2] = type;
  memcpy(f + 3, text, n);
  f[3 + n] = crc8_d5(f + 2, n + 1);
  Serial.write(f, n + 4);
}

static void status(const char *fmt, ...)
{
  char b[FRAME_MAX];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  send_ctrl(0x7D, b, n < 0 ? 0 : (unsigned)n < sizeof(b) ? n : sizeof(b) - 1);
}

// ---- elrs_host_api_t ------------------------------------------------------

static const struct { GPIO_TypeDef *port; uint8_t pin; } spi_cs[] = {
  { GPIOC, 1 },   // ELRS_SPI_SX1272
  { GPIOE, 8 },   // ELRS_SPI_GIMBAL0
  { GPIOD, 7 },   // ELRS_SPI_GIMBAL1
};

static void spi2_setup(void)
{
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_GPIOEEN;
  RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
  (void)RCC->APB1ENR;
  for (auto &c : spi_cs) {
    c.port->BSRR = 1u << c.pin;
    c.port->MODER = (c.port->MODER & ~(3u << (2 * c.pin))) | (1u << (2 * c.pin));
  }
  struct { GPIO_TypeDef *p; int n; } af[] = { { GPIOB, 10 }, { GPIOC, 2 }, { GPIOC, 3 } };
  for (auto &a : af) {
    a.p->OSPEEDR |= 3u << (2 * a.n);
    a.p->AFR[a.n >> 3] = (a.p->AFR[a.n >> 3] & ~(0xFu << (4 * (a.n & 7)))) | (5u << (4 * (a.n & 7)));
    a.p->MODER = (a.p->MODER & ~(3u << (2 * a.n))) | (2u << (2 * a.n));
  }
  SPI2->CR1 = 0;
  // master, mode 0, 8-bit, MSB first, software NSS, PCLK1/4 = 10.5 MHz
  SPI2->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | (1u << SPI_CR1_BR_Pos) | SPI_CR1_SPE;
}

static void h_spi_xfer(uint8_t dev, const uint8_t *tx, uint8_t *rx, uint16_t n)
{
  if (dev >= sizeof(spi_cs) / sizeof(spi_cs[0])) return;
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  spi_cs[dev].port->BSRR = 1u << (spi_cs[dev].pin + 16);
  for (uint16_t i = 0; i < n; i++) {
    while (!(SPI2->SR & SPI_SR_TXE)) {}
    *(volatile uint8_t *)&SPI2->DR = tx ? tx[i] : 0;
    while (!(SPI2->SR & SPI_SR_RXNE)) {}
    uint8_t b = *(volatile uint8_t *)&SPI2->DR;
    if (rx) rx[i] = b;
  }
  while (SPI2->SR & SPI_SR_BSY) {}
  spi_cs[dev].port->BSRR = 1u << spi_cs[dev].pin;
  __set_PRIMASK(primask);
}

static uint16_t h_crsf_read(uint8_t *buf, uint16_t max)
{
  if (rxq_tail == rxq_head) return 0;
  uint16_t n = rxq_len[rxq_tail];
  if (n > max) n = max;
  memcpy(buf, rxq[rxq_tail], n);
  rxq_tail = (rxq_tail + 1) % RXQ_LEN;
  return n;
}

static void h_crsf_write(const uint8_t *frame, uint16_t len) { Serial.write(frame, len); }
static uint32_t h_micros(void) { return micros(); }
static uint32_t h_millis(void) { return millis(); }
static void h_delay_us(uint32_t us) { delayMicroseconds(us); }

static int h_config_load(void *buf, uint32_t len)
{
  if (!cfg_len) return -1;
  uint32_t n = len < cfg_len ? len : cfg_len;
  memcpy(buf, cfg_blob, n);
  return n;
}

static int h_config_save(const void *buf, uint32_t len)
{
  if (len > sizeof(cfg_blob)) return -1;
  memcpy(cfg_blob, buf, len);
  cfg_len = len;
  return 0;
}

static void h_log(const char *fmt, ...)
{
  char b[FRAME_MAX];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if ((unsigned)n >= sizeof(b)) n = sizeof(b) - 1;
  send_ctrl(0x7F, b, n);
}

static int16_t h_crystal_ppm_x10(void) { return -159; }  // this unit, measured 2026-11-10
static uint8_t h_hw_rev(void) { return *(volatile uint32_t *)0x08007F00u & 0xFF; }  // TBS hwId, 0x00040005

static const elrs_host_api_t host = {
  ELRS_ABI_MAJOR, ELRS_ABI_MINOR, h_spi_xfer, h_crsf_read, h_crsf_write, h_micros, h_millis,
  h_delay_us, h_config_load, h_config_save, h_log, h_crystal_ppm_x10, h_hw_rev,
};

// ---- USB framing ------------------------------------------------------------

// Collects frames from the USB stream; returns true when the stop frame arrives.
static bool pump_usb(void)
{
  static uint8_t f[FRAME_MAX + 2];
  static uint8_t n;
  while (Serial.available()) {
    uint8_t b = Serial.read();
    if (n == 0) {
      if (b == 0xC8 || b == 0xEA || b == 0xEE || b == 0xEC || b == 0x7E) f[n++] = b;
      continue;
    }
    if (n == 1 && (b < 2 || b > FRAME_MAX - 2)) { n = 0; continue; }
    f[n++] = b;
    if (n < 2 || n < f[1] + 2) continue;
    unsigned len = n;
    n = 0;
    if (crc8_d5(f + 2, f[1] - 1) != f[len - 1]) continue;
    if (f[0] == 0x7E) {
      if (f[2] == 0x01) return true;
      continue;
    }
    uint8_t next = (rxq_head + 1) % RXQ_LEN;
    if (next == rxq_tail) continue;  // full: drop
    memcpy(rxq[rxq_head], f, len);
    rxq_len[rxq_head] = len;
    rxq_head = next;
  }
  return false;
}

void image_host_run(void)
{
  const elrs_image_header_t *h = (const elrs_image_header_t *)ELRS_IMAGE_ADDR;
  if (h->magic != ELRS_IMAGE_MAGIC) { Serial.print("ERR no image (magic)\n"); return; }
  if (h->abi_major != ELRS_ABI_MAJOR) { Serial.printf("ERR image ABI %u, host %u\n", h->abi_major, ELRS_ABI_MAJOR); return; }
  if (h->image_size < sizeof(*h) || h->image_size > ELRS_IMAGE_MAX) { Serial.print("ERR image size\n"); return; }
  uint32_t crc = crc32((const uint8_t *)ELRS_IMAGE_ADDR + sizeof(*h), h->image_size - sizeof(*h));
  if (crc != h->image_crc32) { Serial.printf("ERR image crc %08lx != %08lx\n", (unsigned long)crc, (unsigned long)h->image_crc32); return; }
  if (h->ram_start < ELRS_IMAGE_RAM_START || h->ram_end > ELRS_IMAGE_RAM_END) { Serial.print("ERR image RAM window\n"); return; }

  Serial.printf("IMAGE %s, %lu bytes, ABI %u.%u: switching to framed CRSF\n", h->version ? h->version : "?",
                (unsigned long)h->image_size, h->abi_major, h->abi_minor);
  Serial.flush();
  rxq_head = rxq_tail = 0;

  spi2_setup();
  __disable_irq();
  int rc = h->init(&host);
  if (rc) {
    __enable_irq();
    status("init failed rc=%d", rc);
    Serial.print("OK\n");
    return;
  }
  uint32_t old_vtor = SCB->VTOR;
  memcpy(ram_vtor, (const void *)old_vtor, sizeof(ram_vtor));
  if (h->irq_exti15_10) ram_vtor[16 + EXTI15_10_IRQn] = (uint32_t)h->irq_exti15_10;
  if (h->irq_timer) ram_vtor[16 + h->timer_irqn] = (uint32_t)h->irq_timer;
  SCB->VTOR = (uint32_t)ram_vtor;
  __DSB();
  __enable_irq();
  status("image started");
  h->start();

  while (!pump_usb()) h->loop();

  h->stop();
  __disable_irq();
  NVIC_DisableIRQ(EXTI15_10_IRQn);
  if (h->irq_timer) NVIC_DisableIRQ((IRQn_Type)h->timer_irqn);
  SCB->VTOR = old_vtor;
  __DSB();
  __enable_irq();
  status("image stopped");
  Serial.print("OK\n");
}
