/* SiFive Universal Asynchronous Receiver/Transmitter implementation
* see: SiFive FE310-G003 Manual, Chapter 17 */

#ifndef RV_UART_H
#define RV_UART_H

#include "rv.h"

/* UART I/O callback, *byte is data. similar to bus access callback. */
typedef rv_res (*rv_uart_cb)(void *user, u8 *byte, u32 is_write);

#define RV_UART_FIFO_SIZE 8U

/* internal FIFO instance used by the UART */
typedef struct rv_uart_fifo {
  u8 buf[RV_UART_FIFO_SIZE];
  u32 read, size;
} rv_uart_fifo;

typedef struct rv_uart {
  rv_uart_cb cb;
  void *user;
  rv_uart_fifo rx, tx;
  u32 txctrl, rxctrl, ip, ie, div, clk;
} rv_uart;

/* initialize a UART with a user-provided I/O callback */
void rv_uart_init(rv_uart *uart, void *user, rv_uart_cb cb);

#define RV_UART_SIZE /* size of memory map */ 0x20

/* perform a bus access on the UART */
rv_res rv_uart_bus(rv_uart *uart, u32 addr, u8 *data, u32 is_store,
                   u32 width);

/* update the UART */
u32 rv_uart_update(rv_uart *uart);

#endif
