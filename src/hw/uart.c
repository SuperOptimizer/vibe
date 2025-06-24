#include <curses.h>


#include "vibe.h"

rv_res uart_io(u8 *byte, bool write) {
  int ch;
  static int thrott = 0; /* prevent getch() from being called too much */
  if (write && *byte != '\r') /* curses bugs out if we echo '\r' */
    echochar(*byte);
  else if (!write && ((thrott = (thrott + 1) & 0xFFF) || (ch = getch()) == ERR))
    return RV_BAD;
  else if (!write)
    *byte = (u8)ch;
  return RV_OK;
}

void hw_uart_fifo_init(hw_uart_fifo *fifo) { memset(fifo, 0, sizeof(*fifo)); }

void hw_uart_fifo_put(hw_uart_fifo *fifo, u8 byte) {
  if (fifo->size == hw_uart_FIFO_SIZE)
    return;
  fifo->buf[(fifo->read + fifo->size) & (hw_uart_FIFO_SIZE - 1)] = byte;
  fifo->size++;
}

u8 hw_uart_fifo_get(hw_uart_fifo *fifo) {
  u8 ch = fifo->buf[fifo->read];
  if (!fifo->size)
    return 0;
  fifo->read = (fifo->read + 1) & (hw_uart_FIFO_SIZE - 1);
  fifo->size--;
  return ch;
}

void hw_uart_init(hw_uart *uart) {
  memset(uart, 0, sizeof(*uart));
  uart->div = 3;
  hw_uart_fifo_init(&uart->tx);
  hw_uart_fifo_init(&uart->rx);
}

bus_error hw_uart_bus(hw_uart *uart, u32 addr, u8 *d, bool is_store, u32 width) {
  u32 data;
  rv_endcvt(d, (u8 *)&data, 4, 0);
  if (width != 4)
    return BUS_ALIGN;
  if (addr == 0x00) {
    /*R txdata */
    if (is_store)
      hw_uart_fifo_put(&uart->tx, (u8)data);
    else
      data = (u32)(uart->tx.size == hw_uart_FIFO_SIZE) << 31U;
  } else if (addr == 0x04) {
    /*R rxdata */
    if (!is_store)
      data = ((u32)(!uart->rx.size) << 31U) | hw_uart_fifo_get(&uart->rx);
  } else if (addr == 0x08) {
    /*R txctrl */
    if (is_store)
      uart->txctrl = data & 0x00070003; /* no nstop supported */
    else
      data = uart->txctrl;
  } else if (addr == 0x0C) {
    /*R rxctrl */
    if (is_store)
      uart->rxctrl = data & 0x00070001;
    else
      data = uart->rxctrl;
  } else if (addr == 0x10) {
    /*R ie */
    if (is_store)
      uart->ie = data & 3;
    else
      data = uart->ie;
  } else if (addr == 0x14) {
    /*R ip */
    if (!is_store)
      data = uart->ip;
  } else if (addr == 0x18) {
    /*R div */
    if (is_store)
      uart->div = data & 0xFFFF;
    else
      data = uart->div;
  } else if (addr == 0x1C) {
    /*R unused */
    if (!is_store)
      data = 0;
  } else {
    return BUS_UNMAPPED;
  }
  rv_endcvt((u8 *)&data, d, 4, 1);
  return BUS_OK;
}

u32 hw_uart_update(hw_uart *uart) {
  u8 byte = uart->tx.buf[uart->tx.read];
  if (++uart->clk >= uart->div) {
    if ((uart->txctrl & 1) && uart->tx.size && (uart_io(&byte, true) == RV_OK))
      hw_uart_fifo_get(&uart->tx);
    if ((uart->rxctrl & 1) && (uart->rx.size < hw_uart_FIFO_SIZE) && (uart_io(&byte, false) == RV_OK))
      hw_uart_fifo_put(&uart->rx, byte);
    uart->clk = 0;
  }
  if ((uart->txctrl >> 16) > uart->tx.size && (uart->ie & 1))
    uart->ip |= 1;
  else
    uart->ip &= ~(1U);
  if ((uart->rxctrl >> 16) < uart->rx.size && (uart->ie & 2))
    uart->ip |= 2;
  else
    uart->ip &= ~(2U);
  return !!uart->ip;
}