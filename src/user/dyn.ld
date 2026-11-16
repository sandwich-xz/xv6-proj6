ENTRY( main )

SECTIONS
{
  /* 起始页留给 ELF 头和程序头，保证第一个 LOAD 覆盖它们 */
  . = 0x10000 + SIZEOF_HEADERS;

  .interp : { *(.interp) }

  .text : {
    *(.text .text.*)
  }

  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*)
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  .eh_frame : {
    *(.eh_frame)
    *(.eh_frame.*)
  }

  . = ALIGN(0x1000);
  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*)
    . = ALIGN(16);
    *(.data .data.*)
  }

  .bss : {
    . = ALIGN(16);
    *(.sbss .sbss.*)
    . = ALIGN(16);
    *(.bss .bss.*)
  }

  PROVIDE(end = .);
}
