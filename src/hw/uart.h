#pragma once

#define hw_uart_SIZE  0x20
#define hw_uart_FIFO_SIZE 8U

typedef struct hw_uart_fifo {
  u8 buf[hw_uart_FIFO_SIZE];
  u32 read, size;
} hw_uart_fifo;

struct hw_uart {
  mach* mach;
  hw_uart_fifo rx, tx;
  u32 txctrl, rxctrl, ip, ie, div, clk;
};

void hw_uart_init(hw_uart *uart);
bus_error hw_uart_bus(hw_uart *uart, u32 addr, u8 *data, bool is_store, u32 width);
u32 hw_uart_update(hw_uart *uart);

