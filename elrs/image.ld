/* Tango 2 ELRS image: same layout as abi/test_image/image.ld, plus C++
   constructor tables and a heap at the top of the image RAM window. */
MEMORY {
  FLASH (rx)  : ORIGIN = 0x080E8000, LENGTH = 0x18000  /* abi ELRS_IMAGE_ADDR/MAX */
  RAM   (rwx) : ORIGIN = 0x20018000, LENGTH = 32K
}
SECTIONS {
  .header : { KEEP(*(.image_header)) } > FLASH
  .text : {
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
    __preinit_array_start = .;
    KEEP(*(.preinit_array*))
    __preinit_array_end = .;
    __init_array_start = .;
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    __init_array_end = .;
    . = ALIGN(4);
  } > FLASH
  .data : { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > RAM AT > FLASH
  __data_load = LOADADDR(.data);
  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(8); __bss_end = .; } > RAM
  __heap_start = __bss_end;
  __heap_end = ORIGIN(RAM) + LENGTH(RAM);
  __image_end = __data_load + (__data_end - __data_start);
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.fini_array*) *(.comment) }
}
