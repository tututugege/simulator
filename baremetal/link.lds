OUTPUT_ARCH( "riscv" )
ENTRY(_start)


MEMORY
{
  ram (wxa!ri) :   ORIGIN = 0x80000000, LENGTH = 128M
}


SECTIONS
{
   __stack_size = DEFINED(__stack_size) ? __stack_size : 32M;


  .init           :
  {
    KEEP (*(SORT_NONE(.init)))
  } >ram AT>ram 

  .text           :
  {
    *(.text.unlikely .text.unlikely.*)
    *(.text.startup .text.startup.*)
    *(.text .text.*)
    *(.gnu.linkonce.t.*)
  } >ram AT>ram 

  . = ALIGN(8);
  PROVIDE( end = . );
  .data          :
  {
    . = ALIGN(8);
    PROVIDE( __global_pointer = . + 0x800 );
    . = ALIGN(8);
  } >ram AT>ram 

  .stack ORIGIN(ram) + LENGTH(ram) - __stack_size :
  {
    . = __stack_size;
    PROVIDE( _sp = . );
  } >ram AT>ram

}
