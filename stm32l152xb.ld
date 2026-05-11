/* Linker script for STM32L152RB */
MEMORY
{
  FLASH (rx)     : ORIGIN = 0x08000000, LENGTH = 128K
  
  /* Выделяем 4 байта под ящик для ST-Link */
  MAILBOX (xrw)  : ORIGIN = 0x20000000, LENGTH = 4
  
  /* Основная память сдвигается на 4 байта (16K - 4 = 16380) */
  RAM (xrw)      : ORIGIN = 0x20000004, LENGTH = 16380
}

ENTRY(Reset_Handler)

SECTIONS
{
  /* Наша фиксированная секция. NOLOAD означает, что код инициализации её не трогает */
  .mailbox_section (NOLOAD) :
  {
    . = ALIGN(4);
    KEEP(*(.mailbox_section))
  } >MAILBOX

  .text :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    *(.text)
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

  _sidata = LOADADDR(.data);

  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  /_stack :
  {
    . = ALIGN(8);
    . = . + 0x400;
    _estack = .;
  } >RAM

  ._user_heap_stack :
  {
    . = ALIGN(4);
    PROVIDE ( _end = . );
    PROVIDE ( __end__ = . );
    . = . + 0x400;
    . = ALIGN(4);
  } >RAM
}
