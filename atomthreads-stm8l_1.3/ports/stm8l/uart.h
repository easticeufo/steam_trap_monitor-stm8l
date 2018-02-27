/*
 * Copyright (c) 2013, Wei Shuai <cpuwolf@gmail.com>. All rights reserved.
 *
 */
#include <atom.h>
#include <atommutex.h>
#include <atomsem.h>

#define MAX_USART_BUFF_SIZE 64

typedef struct
{
	uint8_t buff[MAX_USART_BUFF_SIZE];
	uint8_t rd_idx;
	uint8_t wr_idx;
}USART_BUFF;

typedef struct
{
	ATOM_MUTEX mutex;
	ATOM_SEM recv_sem;
	USART_BUFF recv_buff;
}USART_OBJECT;

/*
 * Perform UART startup initialization.
 */
int debug_uart_init(uint32_t baudrate);
void serial_recv_isr_uart1(void);
void serial_recv_isr_uart2(void);
void serial_recv_isr_uart3(void);

int8_t debug_serial_read_onebyte(uint8_t *byte);

int gprs_uart_init(uint32_t baudrate);
int8_t gprs_serial_read_onebyte(uint8_t *byte, int32_t timeout);
void gprs_serial_send_onebyte (uint8_t byte);
void clear_gprs_buffer(void);

int xbee_uart_init(uint32_t baudrate);
int8_t xbee_serial_read_onebyte(uint8_t *byte, int32_t timeout);
void xbee_serial_send_onebyte (uint8_t byte);
void xbee_clear_buffer(void);

