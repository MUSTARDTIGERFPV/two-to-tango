// The image header at 0x080E8000 (abi/elrs_image.h). In C so the flash
// address of the .data initialiser is a link-time constant.
#include "elrs_image.h"
#include "stm32f4xx.h"

extern char __data_load[];
extern const char version[];
int tango2_init(const elrs_host_api_t *host);
void tango2_start(void);
void tango2_loop(void);
void tango2_stop(void);
void tango2_irq_exti(void);
void tango2_irq_timer(void);

__attribute__((section(".image_header"), used)) const elrs_image_header_t image_header = {
  ELRS_IMAGE_MAGIC, ELRS_ABI_MAJOR, ELRS_ABI_MINOR,
  0, 0,  // image_size, image_crc32: filled by abi/finish_image.py
  version,
  ELRS_IMAGE_RAM_START, ELRS_IMAGE_RAM_END, (uint32_t)__data_load,
  tango2_init, tango2_start, tango2_loop, tango2_stop,
  tango2_irq_exti, tango2_irq_timer,
  (uint8_t)TIM5_IRQn,
  { 0, 0, 0 },
};
