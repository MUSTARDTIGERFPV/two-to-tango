// Tango 2 resident loader/monitor. Lives at 0x0800C000-0x0801FFFF (loaded
// through the TBS bootloader); boots EdgeTX at 0x08020000, reflashes EdgeTX
// and the ELRS image (0x080C0000) over USB, runs RAM tasks, and hosts the ELRS
// image for development (image_host.cpp). It never writes its own sectors or
// the TBS bootloader, and peek/poke refuses flash, the flash interface
// registers and its own RAM.
//
// Line commands over USB CDC (numbers accept 0x.. or decimal):
//   I                    info: chip, clocks, memory map, API version
//   F                    last fault record (survives the reset), then clears it
//   R <addr> <len>       "DATA <addr> <len>\n" + raw bytes + "CRC <crc32>\n"
//   X <addr> [<n>]       read n 32-bit words as hex text (default 1)
//   Y <addr> <val> [<w>] write val with width w = 1, 2 or 4 bytes (default 4)
//   L <addr> <len> <crc> then <len> raw bytes: load into the task area, check CRC32
//   C <addr> [args]      run the task image at addr; ends with "TASK RC <rc>"
//   Z                    reset (the bootloader then starts the monitor again)
//   B                    boot EdgeTX (reset without the stay request)
//   E <addr> <len> [ERASE-TBS]  erase the flash sectors covering the range;
//                        only 0x08020000-0x080FFFFF, sector-aligned. Sectors
//                        10-11 (the TBS Crossfire image) also need ERASE-TBS.
//   W <addr> <len> <crc> then <len> raw bytes: program erased flash in the
//                        same range (word aligned), then check CRC32 in flash
//   G                    host the ELRS image at 0x080C0000: the USB link switches
//                        to framed CRSF until the PC sends the stop frame (see
//                        image_host.cpp for the framing)
// Holding the power button 2 s resets (USB present) or powers off (battery).
//
// Resident loader: at reset this firmware boots EdgeTX at 0x08020000 unless
// RTC->BKP19R == "MON1" (cleared here), a key (PD4/PD12/PD13/PD14) is held,
// or there is no valid EdgeTX image there, or EdgeTX failed to start 3 times
// in a row (BKP18R crash counter; EdgeTX clears it once healthy).
// Every command ends with "OK" or "ERR <reason>".
#include <Arduino.h>
#include <stdarg.h>
#include "task_api.h"

void image_host_run(void);  // image_host.cpp

static const uint32_t PWR_ON_PIN = PB12;  // power hold, must stay high

// ---- resident loader: boot decision, before any HAL/clock/USB init -------

#define APP_ADDR      0x08020000u
#define APP_END       0x080C0000u
#define IMAGE_END     0x08100000u
#define STAY_MAGIC    0x4D4F4E31u  // "MON1" in RTC->BKP19R
#define KEY_PINS_MASK ((1u << 4) | (1u << 12) | (1u << 13) | (1u << 14))
#define BOOT_TAG      0xB0070000u  // RTC->BKP18R: crash-loop counter (low byte)
// RTC->BKP17R is free for the app's boot breadcrumbs; shown by 'I'.
// RTC->BKP16R: RCC->CSR as it was at reset, for the app (we clear the flags).

static uint8_t boot_reason;  // why we stayed: 1 magic, 2 key, 3 no app, 4 app failed 3x
static uint32_t reset_flags;

static bool app_valid(void)
{
  uint32_t sp = *(volatile uint32_t *)APP_ADDR, rv = *(volatile uint32_t *)(APP_ADDR + 4);
  bool sp_ok = (sp >= 0x20000000u && sp <= 0x20020000u) || (sp >= 0x10000000u && sp <= 0x10010000u);
  return sp_ok && (rv & 1) && rv > APP_ADDR && rv < APP_END;
}

__attribute__((constructor(101))) static void loader_boot_decision(void)
{
  reset_flags = RCC->CSR;  // IWDGRSTF = a hung app was reset
  RCC->CSR |= RCC_CSR_RMVF;
  RCC->APB1ENR |= RCC_APB1ENR_PWREN;
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
  (void)RCC->AHB1ENR;
  PWR->CR |= PWR_CR_DBP;
  RTC->BKP16R = reset_flags;  // the app's view of why we reset (RCC->CSR before RMVF)
  PWR->CR &= ~PWR_CR_DBP;
  bool magic = RTC->BKP19R == STAY_MAGIC;
  if (magic) {
    PWR->CR |= PWR_CR_DBP;
    RTC->BKP19R = 0;
    PWR->CR &= ~PWR_CR_DBP;
  }
  // keys: inputs with pull-up, active low
  uint32_t pupd = GPIOD->PUPDR, moder = GPIOD->MODER;
  for (int n : { 4, 12, 13, 14 }) {
    GPIOD->MODER &= ~(3u << (2 * n));
    GPIOD->PUPDR = (GPIOD->PUPDR & ~(3u << (2 * n))) | (1u << (2 * n));
  }
  for (volatile int i = 0; i < 20000; i++) {}  // let the pull-ups settle
  bool key = (GPIOD->IDR & KEY_PINS_MASK) != KEY_PINS_MASK;
  GPIOD->PUPDR = pupd;
  GPIOD->MODER = moder;

  // Crash-loop guard: BKP18R = BOOT_TAG | attempts. The app clears it once it
  // is healthy; three unconfirmed starts in a row keep us in the loader.
  uint32_t tries = (RTC->BKP18R & 0xFFFFFF00u) == BOOT_TAG ? (RTC->BKP18R & 0xFF) : 0;
  boot_reason = magic ? 1 : key ? 2 : !app_valid() ? 3 : tries >= 3 ? 4 : 0;
  PWR->CR |= PWR_CR_DBP;
  RTC->BKP18R = boot_reason ? 0 : BOOT_TAG | (tries + 1);
  PWR->CR &= ~PWR_CR_DBP;
  if (boot_reason) return;

  // A hung app gets reset by the IWDG (~4 s; EdgeTX reprograms and kicks it).
  IWDG->KR = 0xCCCC;
  IWDG->KR = 0x5555;
  IWDG->PR = 6;      // LSI ~32 kHz / 256
  IWDG->RLR = 500;   // ~4 s
  IWDG->KR = 0xAAAA;

  // Hand over to EdgeTX the way the TBS bootloader hands over to us.
  __disable_irq();
  SysTick->CTRL = 0;
  for (int i = 0; i < 8; i++) NVIC->ICER[i] = 0xFFFFFFFFu, NVIC->ICPR[i] = 0xFFFFFFFFu;
  SCB->VTOR = APP_ADDR;
  uint32_t sp = *(volatile uint32_t *)APP_ADDR, rv = *(volatile uint32_t *)(APP_ADDR + 4);
  __set_MSP(sp);
  __enable_irq();
  ((void (*)(void))rv)();
  for (;;) {}
}

// Power button escape hatch, polled from SysTick so it works while a task runs
// (unless the task blocks interrupts). PB14 reads high while the button is held
// (EdgeTX boardOff waits on exactly that). Held for 2 s: with USB VBUS (PA9)
// present, reset (hold key E as well to land in the TBS bootloader); on battery,
// drop power hold. Armed only after the button has been seen released, so the
// press that turned the radio on doesn't count.
#define PWR_HOLD_MS 2000
static bool pwr_armed;
static uint32_t pwr_held_ms;

static bool pwr_pressed() { return GPIOB->IDR & (1u << 14); }
static bool vbus_present() { return GPIOA->IDR & (1u << 9); }

extern "C" void HAL_SYSTICK_Callback(void)
{
  if (!pwr_pressed()) {
    pwr_armed = true;
    pwr_held_ms = 0;
    return;
  }
  if (!pwr_armed || ++pwr_held_ms < PWR_HOLD_MS) return;
  if (vbus_present()) NVIC_SystemReset();
  GPIOB->BSRR = 1u << (12 + 16);  // power hold low: the radio switches off
  for (;;) {
    if (!pwr_pressed() && vbus_present()) NVIC_SystemReset();  // USB came back while held off
  }
}

struct fault_record_t {
  uint32_t magic;
  uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr;
  uint32_t cfsr, hfsr, mmfar, bfar, exc_return;
};
#define FAULT_MAGIC 0x544C5546u  // "FAULT"
static fault_record_t *const fault = (fault_record_t *)FAULT_RECORD;

extern "C" void SystemClock_Config(void)
{
  RCC_OscInitTypeDef osc = {};
  RCC_ClkInitTypeDef clk = {};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  // 8 MHz HSE -> 168 MHz SYSCLK, 48 MHz USB. Try crystal, then bypass, then HSI.
  const uint32_t hse_modes[] = { RCC_HSE_ON, RCC_HSE_BYPASS };
  bool ok = false;
  for (uint32_t mode : hse_modes) {
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = mode;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 8;
    osc.PLL.PLLN = 336;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&osc) == HAL_OK) { ok = true; break; }
  }
  if (!ok) {
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSEState = RCC_HSE_OFF;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM = 16;
    HAL_RCC_OscConfig(&osc);
  }

  clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV4;
  clk.APB2CLKDivider = RCC_HCLK_DIV2;
  HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5);
}

// Any fault (tasks included) lands here: record it in RAM that survives the
// reset, then reset so the monitor comes back on its own.
extern "C" void fault_capture(uint32_t *frame, uint32_t exc_return)
{
  fault->r0 = frame[0];
  fault->r1 = frame[1];
  fault->r2 = frame[2];
  fault->r3 = frame[3];
  fault->r12 = frame[4];
  fault->lr = frame[5];
  fault->pc = frame[6];
  fault->xpsr = frame[7];
  fault->cfsr = SCB->CFSR;
  fault->hfsr = SCB->HFSR;
  fault->mmfar = SCB->MMFAR;
  fault->bfar = SCB->BFAR;
  fault->exc_return = exc_return;
  fault->magic = FAULT_MAGIC;
  NVIC_SystemReset();
}

extern "C" __attribute__((naked)) void HardFault_Handler(void)
{
  __asm volatile(
    "tst lr, #4      \n"
    "ite eq          \n"
    "mrseq r0, msp   \n"
    "mrsne r0, psp   \n"
    "mov r1, lr      \n"
    "b fault_capture \n");
}
extern "C" void MemManage_Handler(void) __attribute__((alias("HardFault_Handler")));
extern "C" void BusFault_Handler(void) __attribute__((alias("HardFault_Handler")));
extern "C" void UsageFault_Handler(void) __attribute__((alias("HardFault_Handler")));

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
  }
  return crc;
}

static bool in(uint32_t a, uint32_t len, uint32_t lo, uint32_t hi)
{
  return a >= lo && a + len <= hi && a + len >= a;
}

// Where the host may write: task area, CCM, peripherals (minus the flash
// interface) and the Cortex-M system space. Never flash or monitor RAM.
static bool writable(uint32_t a, uint32_t len)
{
  if (in(a, len, TASK_AREA_START, TASK_AREA_END)) return true;
  if (in(a, len, 0x10000000u, 0x10010000u)) return true;
  if (in(a, len, 0x40000000u, 0x40023C00u)) return true;
  if (in(a, len, 0x40024000u, 0x60000000u)) return true;
  if (in(a, len, 0xE0000000u, 0xE0100000u)) return true;
  return false;
}

// ---- task API -------------------------------------------------------------

static volatile bool abort_flag;
static int pending = -1;

static void api_poll()
{
  while (pending < 0 && Serial.available()) {
    int c = Serial.read();
    if (c == 0x03) abort_flag = true;
    else pending = c;
  }
}

static int api_printf(const char *fmt, ...)
{
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n > 0) Serial.write((const uint8_t *)buf, n < (int)sizeof(buf) ? n : sizeof(buf) - 1);
  return n;
}

static void api_write(const void *buf, uint32_t len) { Serial.write((const uint8_t *)buf, len); }

static int api_getc()
{
  api_poll();
  int c = pending;
  pending = -1;
  return c;
}

static int api_abort()
{
  api_poll();
  return abort_flag;
}

static uint32_t api_millis() { return millis(); }
static uint32_t api_micros() { return micros(); }
static void api_delay_ms(uint32_t ms) { delay(ms); }
static void api_delay_us(uint32_t us) { delayMicroseconds(us); }

static const task_api_t api = {
  TASK_API_VERSION, api_printf, api_write, api_getc, api_abort,
  api_millis, api_micros, api_delay_ms, api_delay_us,
};

// ---- commands -------------------------------------------------------------

static void ok() { Serial.print("OK\n"); }
static void err(const char *why) { Serial.printf("ERR %s\n", why); }

static void cmd_info()
{
  Serial.printf("MONITOR tango2 api %u\n", (unsigned)TASK_API_VERSION);
  Serial.printf("IDCODE %08lx FLASHSIZE_KB %u\n", (unsigned long)DBGMCU->IDCODE,
                (unsigned)*(volatile uint16_t *)0x1FFF7A22);
  Serial.printf("SYSCLK %lu HCLK %lu PCLK1 %lu PCLK2 %lu\n", (unsigned long)HAL_RCC_GetSysClockFreq(),
                (unsigned long)HAL_RCC_GetHCLKFreq(), (unsigned long)HAL_RCC_GetPCLK1Freq(),
                (unsigned long)HAL_RCC_GetPCLK2Freq());
  Serial.printf("TASK_AREA %08lx %08lx\n", (unsigned long)TASK_AREA_START, (unsigned long)TASK_AREA_END);
  Serial.printf("FAULT %s\n", fault->magic == FAULT_MAGIC ? "pending" : "none");
  static const char *const why[] = { "?", "stay request (BKP19R)", "key held", "no EdgeTX image",
                                     "EdgeTX failed to start 3 times" };
  Serial.printf("LOADER stayed: %s; EdgeTX at %08lx %s; BKP17R breadcrumb %08lx; reset flags %08lx\n",
                why[boot_reason <= 4 ? boot_reason : 0], (unsigned long)APP_ADDR, app_valid() ? "valid" : "absent",
                (unsigned long)RTC->BKP17R, (unsigned long)reset_flags);
  Serial.printf("PWR_BTN %d VBUS %d armed %d\n", pwr_pressed(), vbus_present(), pwr_armed);
  ok();
}

static void cmd_fault()
{
  if (fault->magic != FAULT_MAGIC) { Serial.print("no fault recorded\n"); ok(); return; }
  Serial.printf("pc %08lx lr %08lx xpsr %08lx\n", (unsigned long)fault->pc, (unsigned long)fault->lr,
                (unsigned long)fault->xpsr);
  Serial.printf("r0 %08lx r1 %08lx r2 %08lx r3 %08lx r12 %08lx\n", (unsigned long)fault->r0,
                (unsigned long)fault->r1, (unsigned long)fault->r2, (unsigned long)fault->r3,
                (unsigned long)fault->r12);
  Serial.printf("cfsr %08lx hfsr %08lx mmfar %08lx bfar %08lx exc_return %08lx\n",
                (unsigned long)fault->cfsr, (unsigned long)fault->hfsr, (unsigned long)fault->mmfar,
                (unsigned long)fault->bfar, (unsigned long)fault->exc_return);
  fault->magic = 0;
  ok();
}

static void cmd_read(uint32_t a, uint32_t len)
{
  if (len == 0 || len > 0x100000u || a + len < a) { err("range"); return; }
  Serial.printf("DATA %08lx %lu\n", (unsigned long)a, (unsigned long)len);
  uint32_t crc = 0xFFFFFFFFu;
  const uint8_t *p = (const uint8_t *)a;
  while (len) {
    uint32_t n = len > 512 ? 512 : len;
    Serial.write(p, n);
    crc = crc32_update(crc, p, n);
    p += n;
    len -= n;
  }
  Serial.printf("CRC %08lx\n", (unsigned long)(crc ^ 0xFFFFFFFFu));
}

static void cmd_peek(uint32_t a, uint32_t n)
{
  if (a & 3) { err("align"); return; }
  if (n == 0) n = 1;
  if (n > 64) n = 64;
  for (uint32_t i = 0; i < n; i++) {
    if (i % 4 == 0) Serial.printf("%s%08lx:", i ? "\n" : "", (unsigned long)(a + 4 * i));
    Serial.printf(" %08lx", (unsigned long)*(volatile uint32_t *)(a + 4 * i));
  }
  Serial.print("\n");
  ok();
}

static void cmd_poke(uint32_t a, uint32_t v, uint32_t w)
{
  if (w == 0) w = 4;
  if ((w != 1 && w != 2 && w != 4) || (a & (w - 1))) { err("width/align"); return; }
  if (!writable(a, w)) { err("protected"); return; }
  if (w == 1) *(volatile uint8_t *)a = v;
  else if (w == 2) *(volatile uint16_t *)a = v;
  else *(volatile uint32_t *)a = v;
  ok();
}

static void cmd_load(uint32_t a, uint32_t len, uint32_t want)
{
  if (!len || !in(a, len, TASK_AREA_START, TASK_AREA_END)) { err("range"); return; }
  Serial.print("READY\n");
  uint8_t *p = (uint8_t *)a;
  uint32_t got = 0, t0 = millis();
  while (got < len) {
    if (Serial.available()) {
      p[got++] = Serial.read();
      t0 = millis();
    } else if (millis() - t0 > 2000) {
      err("timeout");
      return;
    }
  }
  uint32_t crc = crc32_update(0xFFFFFFFFu, p, len) ^ 0xFFFFFFFFu;
  if (crc != want) { Serial.printf("ERR crc %08lx\n", (unsigned long)crc); return; }
  ok();
}

static void cmd_call(uint32_t a, const char *args)
{
  if (!in(a, sizeof(task_header_t), TASK_AREA_START, TASK_AREA_END)) { err("range"); return; }
  const task_header_t *h = (const task_header_t *)a;
  uint32_t entry = (uint32_t)h->entry;
  if (h->magic != TASK_MAGIC) { err("magic"); return; }
  if (!(entry & 1) || !in(entry & ~1u, 2, TASK_AREA_START, TASK_AREA_END)) { err("entry"); return; }
  if (h->bss_end < h->bss_start || !in(h->bss_start, h->bss_end - h->bss_start, TASK_AREA_START, TASK_AREA_END)) {
    err("bss");
    return;
  }
  memset((void *)h->bss_start, 0, h->bss_end - h->bss_start);
  abort_flag = false;
  pending = -1;
  while (*args == ' ') args++;
  Serial.printf("TASK START %08lx\n", (unsigned long)a);
  int rc = h->entry(&api, args);
  Serial.printf("\nTASK RC %d\n", rc);
  ok();
}

// ---- flash erase/program for EdgeTX and the ELRS image --------------------

// STM32F40x sector map from 0x08020000: sectors 5..11, 128 KB each.
static int sector_of(uint32_t a) { return (a >= 0x08020000u && a < IMAGE_END) ? 5 + (a - 0x08020000u) / 0x20000u : -1; }

static void cmd_erase(uint32_t a, uint32_t len, const char *flag)
{
  int first = sector_of(a), last = sector_of(a + len - 1);
  if (!len || first < 0 || last < 0 || (a - 0x08020000u) % 0x20000u) { err("range (sector-aligned, 0x08020000-0x080FFFFF)"); return; }
  if (last >= 10 && !strstr(flag, "ERASE-TBS")) { err("sectors 10-11 hold the TBS image: add ERASE-TBS"); return; }
  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                         FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
  for (int s = first; s <= last; s++) {
    FLASH_EraseInitTypeDef e = {};
    uint32_t bad = 0;
    e.TypeErase = FLASH_TYPEERASE_SECTORS;
    e.Sector = s;
    e.NbSectors = 1;
    e.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    HAL_StatusTypeDef r = HAL_FLASHEx_Erase(&e, &bad);
    Serial.printf("erase sector %d: %s\n", s, r == HAL_OK ? "ok" : "FAILED");
    if (r != HAL_OK) { HAL_FLASH_Lock(); err("erase"); return; }
  }
  HAL_FLASH_Lock();
  ok();
}

static void cmd_write(uint32_t a, uint32_t len, uint32_t want)
{
  if (!len || (a & 3) || (len & 3) || sector_of(a) < 0 || sector_of(a + len - 1) < 0) {
    err("range (word aligned, 0x08020000-0x080FFFFF)");
    return;
  }
  Serial.print("READY\n");
  HAL_FLASH_Unlock();
  uint32_t done = 0, t0 = millis(), word = 0;
  uint8_t nb = 0;
  bool fail = false;
  while (done < len) {
    if (!Serial.available()) {
      if (millis() - t0 > 2000) break;
      continue;
    }
    word |= (uint32_t)Serial.read() << (8 * nb++);
    t0 = millis();
    if (nb == 4) {
      if (*(volatile uint32_t *)(a + done) != 0xFFFFFFFFu ||
          HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, a + done, word) != HAL_OK)
        fail = true;  // keep draining the host's bytes so the stream stays in step
      done += 4;
      word = 0;
      nb = 0;
    }
  }
  HAL_FLASH_Lock();
  if (done < len) { err("timeout"); return; }
  if (fail) { err("program (not erased, or flash error)"); return; }
  uint32_t crc = crc32_update(0xFFFFFFFFu, (const uint8_t *)a, len) ^ 0xFFFFFFFFu;
  if (crc != want) { Serial.printf("ERR crc %08lx\n", (unsigned long)crc); return; }
  ok();
}

void setup()
{
  pinMode(PWR_ON_PIN, OUTPUT);
  digitalWrite(PWR_ON_PIN, HIGH);
  pinMode(PB14, INPUT_PULLUP);  // power button, as EdgeTX configures it
  pinMode(PA9, INPUT);          // USB VBUS
  // Trap divide-by-zero and route MemManage/BusFault/UsageFault to the handler.
  SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;
  SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk;
  Serial.begin(115200);
}

static uint32_t num(char **s)
{
  char *e;
  uint32_t v = strtoul(*s, &e, 0);
  *s = e;
  return v;
}

void loop()
{
  static char line[160];
  static size_t n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (n < sizeof(line) - 1) line[n++] = c;
      continue;
    }
    line[n] = 0;
    n = 0;
    char *s = line + 1;
    switch (line[0]) {
      case 'I': cmd_info(); break;
      case 'F': cmd_fault(); break;
      case 'R': { uint32_t a = num(&s); cmd_read(a, num(&s)); break; }
      case 'X': { uint32_t a = num(&s); cmd_peek(a, num(&s)); break; }
      case 'Y': { uint32_t a = num(&s), v = num(&s); cmd_poke(a, v, num(&s)); break; }
      case 'L': { uint32_t a = num(&s), l = num(&s); cmd_load(a, l, num(&s)); break; }
      case 'C': { uint32_t a = num(&s); cmd_call(a, s); break; }
      case 'Z': ok(); Serial.flush(); delay(50); NVIC_SystemReset(); break;
      case 'B':
        if (!app_valid()) { err("no valid EdgeTX image at 0x08020000"); break; }
        ok(); Serial.flush(); delay(50); NVIC_SystemReset(); break;
      case 'E': { uint32_t a = num(&s), l = num(&s); cmd_erase(a, l, s); break; }
      case 'W': { uint32_t a = num(&s), l = num(&s); cmd_write(a, l, num(&s)); break; }
      case 'G': image_host_run(); break;
      case 0: break;
      default: err("cmd");
    }
  }
}
