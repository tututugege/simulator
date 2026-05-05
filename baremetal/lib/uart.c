#include <stdint.h>

#include "../include/uart.h"
#include "../include/xprintf.h"

// send one char to uart
// void uart_putc(uint8_t c)
// {
//     while (UART0_REG(UART0_STATUS) & 0x1);
//     UART0_REG(UART0_TXDATA) = c;
// }

// // Block, get one char from uart.
// uint8_t uart_getc()
// {
//     UART0_REG(UART0_STATUS) &= ~0x2;
//     while (!(UART0_REG(UART0_STATUS) & 0x2));
//     return (UART0_REG(UART0_RXDATA) & 0xff);
// }

// // 115200bps, 8 N 1
// void uart_init()
// {
//     // enable tx and rx
//     UART0_REG(UART0_CTRL) = 0x3;

//     xdev_out(uart_putc);
// }

static volatile void *uart8250_base;
static u32 uart8250_in_freq;
static u32 uart8250_baudrate;
static u32 uart8250_reg_shift;

static u32 get_reg(u32 num)
{
    u32 offset = num << uart8250_reg_shift;

    return readb(uart8250_base + offset);
}

static void set_reg(u32 num, u32 val)
{
    u32 offset = num << uart8250_reg_shift;

    writeb(val, uart8250_base + offset);
}

void uart_putc(char ch)
{
    // while ((get_reg(UART_LSR_OFFSET) & UART_LSR_THRE) == 0)
    // 	;

    set_reg(UART_THR_OFFSET, ch);
}

int uart_getc(void)
{
    if (get_reg(UART_LSR_OFFSET) & UART_LSR_DR)
        return get_reg(UART_RBR_OFFSET);
    return -1;
}

int uart_init()
{
    u16 bdiv;

    uart8250_base = (volatile void *)0x10000000;
    uart8250_reg_shift = 0;
    uart8250_in_freq = 100000000;
    uart8250_baudrate = 230400;

    bdiv = uart8250_in_freq / (16 * uart8250_baudrate);

    /* Disable all interrupts */
    set_reg(UART_IER_OFFSET, 0x00);
    /* Enable DLAB */
    set_reg(UART_LCR_OFFSET, 0x80);
    /* Set divisor low byte */
    // set_reg(UART_DLL_OFFSET, bdiv & 0xff);
    /* Set divisor high byte */
    set_reg(UART_DLM_OFFSET, (bdiv >> 8) & 0xff);
    /* 8 bits, no parity, one stop bit */
    set_reg(UART_LCR_OFFSET, 0x03);
    /* Enable FIFO */
    set_reg(UART_FCR_OFFSET, 0x01);
    /* No modem control DTR RTS */
    set_reg(UART_MCR_OFFSET, 0x00);
    /* Clear line status */
    // get_reg(UART_LSR_OFFSET);
    // /* Read receive buffer */
    // get_reg(UART_RBR_OFFSET);
    /* Set scratchpad */
    set_reg(UART_SCR_OFFSET, 0x00);

    xdev_out(uart_putc);
    return 0;
}