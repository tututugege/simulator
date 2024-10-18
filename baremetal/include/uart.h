#ifndef _UART_H_
#define _UART_H_

// #define UART0_BASE      (0x30000000)
// #define UART0_CTRL      (UART0_BASE + (0x00))
// #define UART0_STATUS    (UART0_BASE + (0x04))
// #define UART0_BAUD      (UART0_BASE + (0x08))
// #define UART0_TXDATA    (UART0_BASE + (0x0c))
// #define UART0_RXDATA    (UART0_BASE + (0x10))

// #define UART0_REG(addr) (*((volatile uint32_t *)addr))

// void uart_init();
// void uart_putc(uint8_t c);
// uint8_t uart_getc();

// #endif


#define UART_RBR_OFFSET		0	/* In:  Recieve Buffer Register */
#define UART_THR_OFFSET		0	/* Out: Transmitter Holding Register */
#define UART_DLL_OFFSET		0	/* Out: Divisor Latch Low */
#define UART_IER_OFFSET		1	/* I/O: Interrupt Enable Register */
#define UART_DLM_OFFSET		1	/* Out: Divisor Latch High */
#define UART_FCR_OFFSET		2	/* Out: FIFO Control Register */
#define UART_IIR_OFFSET		2	/* I/O: Interrupt Identification Register */
#define UART_LCR_OFFSET		3	/* Out: Line Control Register */
#define UART_MCR_OFFSET		4	/* Out: Modem Control Register */
#define UART_LSR_OFFSET		5	/* In:  Line Status Register */
#define UART_MSR_OFFSET		6	/* In:  Modem Status Register */
#define UART_SCR_OFFSET		7	/* I/O: Scratch Register */
#define UART_MDR1_OFFSET	8	/* I/O:  Mode Register */

#define UART_LSR_FIFOE		0x80    /* Fifo error */
#define UART_LSR_TEMT		0x40    /* Transmitter empty */
#define UART_LSR_THRE		0x20    /* Transmit-hold-register empty */
#define UART_LSR_BI		0x10    /* Break interrupt indicator */
#define UART_LSR_FE		0x08    /* Frame error indicator */
#define UART_LSR_PE		0x04    /* Parity error indicator */
#define UART_LSR_OE		0x02    /* Overrun error indicator */
#define UART_LSR_DR		0x01    /* Receiver data ready */
#define UART_LSR_BRK_ERROR_BITS	0x1E    /* BI, FE, PE, OE bits */


#define u32 uint32_t
#define u16 uint16_t
#define u8 uint16_t

static inline void __raw_writeb(u8 val, volatile void *addr)
{
	asm volatile("sb %0, 0(%1)" : : "r"(val), "r"(addr));
}

static inline u8 __raw_readb(const volatile void *addr)
{
	u8 val;

	asm volatile("lb %0, 0(%1)" : "=r"(val) : "r"(addr));
	return val;
}

#define __io_br()	do {} while (0)
#define __io_ar()	__asm__ __volatile__ ("fence i,r" : : : "memory");
#define __io_bw()	__asm__ __volatile__ ("fence w,o" : : : "memory");
#define __io_aw()	do {} while (0)

#define readb(c)	({ u8  __v; __io_br(); __v = __raw_readb(c); __io_ar(); __v; })

#define writeb(v,c)	({ __io_bw(); __raw_writeb((v),(c)); __io_aw(); })

static u32 get_reg(u32 num);
static void set_reg(u32 num, u32 val);
void uart_putc(char ch);
int uart_getc(void);
int uart_init();

#endif
