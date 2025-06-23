#pragma once

#define RV_UART_SIZE  0x20
#define RV_UART_FIFO_SIZE 8U

typedef struct rv_uart_fifo {
  u8 buf[RV_UART_FIFO_SIZE];
  u32 read, size;
} rv_uart_fifo;

struct rv_uart {
  mach* mach;
  rv_uart_fifo rx, tx;
  u32 txctrl, rxctrl, ip, ie, div, clk;
};

void rv_uart_init(rv_uart *uart);
bus_error rv_uart_bus(rv_uart *uart, u32 addr, u8 *data, bool is_store, u32 width);
u32 rv_uart_update(rv_uart *uart);

