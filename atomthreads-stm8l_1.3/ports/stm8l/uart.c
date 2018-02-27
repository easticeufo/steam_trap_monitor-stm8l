/*
 * Copyright (c) 2013, Wei Shuai <cpuwolf@gmail.com>. All rights reserved.
 *
 * STM8L151K4T6 only supports UART1
 */
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include <stm8l15x.h>

#include <atom.h>
#include <atommutex.h>
#include <atomport.h>
#include <atomsem.h>
#include "base_fun.h"
#include "uart.h"

#ifdef _RAISONANCE_
#define PUTCHAR_PROTOTYPE int putchar (char c)
#define GETCHAR_PROTOTYPE int getchar (void)
#elif defined (_COSMIC_)
#define PUTCHAR_PROTOTYPE char putchar (char c)
#define GETCHAR_PROTOTYPE char getchar (void)
#else /* _IAR_ */
#define PUTCHAR_PROTOTYPE int putchar (int c)
#define GETCHAR_PROTOTYPE int getchar (void)
#endif /* _RAISONANCE_ */
/*
 * Semaphore for single-threaded access to UART device
 */
static USART_OBJECT uart1_obj;

static USART_OBJECT uart2_obj;

static USART_OBJECT uart3_obj;

static INT16 prt_gprs_data = 0; ///< 是否打印gprs模块uart口输出的数据

static INT16 prt_xbee_data = 0; ///< 是否打印xbee模块uart口输出的数据

/*
 * Initialize the UART to requested baudrate, tx/rx, 8N1.
 */
static int uart_init(USART_TypeDef* USARTx, uint32_t baudrate)
{
	USART_OBJECT *p_uart = NULL;

	USART_DeInit(USARTx);

	/* USART configuration */
	USART_Init(USARTx, baudrate,
	         USART_WordLength_8b,
	         USART_StopBits_1,
	         USART_Parity_No,
	         (USART_Mode_TypeDef)(USART_Mode_Tx | USART_Mode_Rx));

	USART_ITConfig(USARTx, USART_IT_RXNE, ENABLE); // 接收采用中断模式

	USART_Cmd(USARTx, ENABLE);
	
	if (USART1 == USARTx)
	{
		p_uart = &uart1_obj;
	}
	else if (USART2 == USARTx)
	{
		p_uart = &uart2_obj;
	}
	else if (USART3 == USARTx)
	{
		p_uart = &uart3_obj;
	}
	else
	{
		return -1;
	}

	memset(p_uart, 0, sizeof(USART_OBJECT));
	/* Create a mutex for single-threaded putchar() access */
	if (atomMutexCreate (&p_uart->mutex) != ATOM_OK)
	{
		return -1;
	}

	if (atomSemCreate (&p_uart->recv_sem, 0) != ATOM_OK)
	{
		return -1;
	}

	/* Finished */
	return 0;
}

/**		  
 * @brief	 读取串口一字节数据，如果无数据则阻塞  
 * @param byte
 * @return 0-成功，-1-错误，-2-超时，-3-无数据 
 */
static int8_t serial_read_onebyte(USART_OBJECT *p_uart, uint8_t *byte, int32_t timeout)
{
	int32_t ret = 0;
	
	if (NULL == byte)
	{
		return -1;
	}

	ret = atomSemGet(&p_uart->recv_sem, timeout);
	if (ATOM_TIMEOUT == ret)
	{
		return -2;
	}
	else if (ret != ATOM_OK)
	{
		return -1;
	}

	if (p_uart->recv_buff.wr_idx == p_uart->recv_buff.rd_idx) // 串口无数据，为空
	{
		return -3;
	}
	
	*byte = p_uart->recv_buff.buff[p_uart->recv_buff.rd_idx];
	p_uart->recv_buff.rd_idx++;
	if (p_uart->recv_buff.rd_idx >= MAX_USART_BUFF_SIZE)
	{
		p_uart->recv_buff.rd_idx = 0;
	}

	return 0;
}

static void serial_put_buff(USART_OBJECT *p_uart, uint8_t data)
{
	p_uart->recv_buff.buff[p_uart->recv_buff.wr_idx] = data;
	p_uart->recv_buff.wr_idx++;

	if (p_uart->recv_buff.wr_idx >= MAX_USART_BUFF_SIZE)
	{
		p_uart->recv_buff.wr_idx = 0;
	}

	if (p_uart->recv_buff.wr_idx == p_uart->recv_buff.rd_idx) // 接收缓冲区满，丢弃最早接收的数据
	{
		p_uart->recv_buff.rd_idx++;
		if (p_uart->recv_buff.rd_idx >= MAX_USART_BUFF_SIZE)
		{
			p_uart->recv_buff.rd_idx = 0;
		}
	}

	atomSemPut(&p_uart->recv_sem);
	return;
}

static void serial_send_onebyte (USART_TypeDef* USARTx, USART_OBJECT *p_uart, uint8_t byte)
{
    /* Block on private access to the UART */
    if (atomMutexGet(&p_uart->mutex, 0) == ATOM_OK)
    {
        /* Write a character to the USART */
         USART_SendData8(USARTx, byte);
        /* Loop until the end of transmission */
        while (USART_GetFlagStatus(USARTx, USART_FLAG_TC) == RESET);


        /* Return mutex access */
        atomMutexPut(&p_uart->mutex);
    }

    return;
}

/**		  
 * @brief	 串口接收数据中断服务  
 * @param 无
 * @return 无  
 */
void serial_recv_isr_uart1(void)
{
	if (USART_GetFlagStatus(USART1, USART_FLAG_OR) == SET)
	{
		USART_ClearFlag(USART1, USART_FLAG_OR);
	}
	serial_put_buff(&uart1_obj, (USART_ReceiveData8(USART1) & 0xFF));
	
	return;
}

void serial_recv_isr_uart2(void)
{
	if (USART_GetFlagStatus(USART2, USART_FLAG_OR) == SET)
	{
		USART_ClearFlag(USART2, USART_FLAG_OR);
	}
	serial_put_buff(&uart2_obj, (USART_ReceiveData8(USART2) & 0xFF));
	
	return;
}

void serial_recv_isr_uart3(void)
{
	if (USART_GetFlagStatus(USART3, USART_FLAG_OR) == SET)
	{
		USART_ClearFlag(USART3, USART_FLAG_OR);
	}
	serial_put_buff(&uart3_obj, (USART_ReceiveData8(USART3) & 0xFF));
	
	return;
}

/*************************************************************
gprs串口相关函数
**************************************************************/
void set_prt_gprs_data(INT16 value)
{
	prt_gprs_data = value;
}

void gprs_debug_thread(UINT32 param)
{
	UINT8 rd_idx = uart2_obj.recv_buff.rd_idx;
	INT8 ch = 0;

	while (1)
	{
		if (uart2_obj.recv_buff.wr_idx == rd_idx) // 串口无数据，为空
		{
			atomTimerDelay(SYSTEM_TICKS_PER_SEC / 2);
			continue;
		}

		ch = (INT8)uart2_obj.recv_buff.buff[rd_idx];
		rd_idx++;
		if (rd_idx >= MAX_USART_BUFF_SIZE)
		{
			rd_idx = 0;
		}
		if (prt_gprs_data != 0)
		{
			printf("%c", ch);
		}
	}
}

/*************************************************************
xbee串口相关函数
**************************************************************/
void set_prt_xbee_data(INT16 value)
{
	prt_xbee_data = value;
}

void xbee_debug_thread(UINT32 param)
{
	UINT8 rd_idx = uart3_obj.recv_buff.rd_idx;
	INT8 ch = 0;

	while (1)
	{
		if (uart3_obj.recv_buff.wr_idx == rd_idx) // 串口无数据，为空
		{
			atomTimerDelay(SYSTEM_TICKS_PER_SEC / 2);
			continue;
		}

		ch = (INT8)uart3_obj.recv_buff.buff[rd_idx];
		rd_idx++;
		if (rd_idx >= MAX_USART_BUFF_SIZE)
		{
			rd_idx = 0;
		}
		if (prt_xbee_data != 0)
		{
			printf("%c", ch);
		}
	}
}

/*
 * GPRS模块串口初始化
 */
int gprs_uart_init(uint32_t baudrate)
{
	return uart_init(USART2, baudrate);
}

/**		  
 * @brief	 读取串口一字节数据，如果无数据则阻塞  
 * @param byte
 * @return 0-成功，-1-错误，-2-超时，-3-无数据  
 */
int8_t gprs_serial_read_onebyte(uint8_t *byte, int32_t timeout)
{
	return serial_read_onebyte(&uart2_obj, byte, timeout);
}

void clear_gprs_buffer(void)
{
	UINT8 ch = 0;
	INT16 ret = 0;
	
	while ((ret = gprs_serial_read_onebyte(&ch, SYSTEM_TICKS_PER_SEC)) != -2)
	{
		if (0 == ret)
		{
			DEBUG_PRINT(DEBUG_WARN, "clear_gprs_buffer recv 0x%02X\n", ch);
		}
		else
		{
			DEBUG_PRINT(DEBUG_ERROR, "clear_gprs_buffer ret=%d\n", ret);
		}
	}
	return;
}

void gprs_serial_send_onebyte (uint8_t byte)
{
	serial_send_onebyte(USART2, &uart2_obj, byte);
    return;
}

/*************************************************************
xbee串口相关函数
**************************************************************/
/*
 * xbee模块串口初始化
 */
int xbee_uart_init(uint32_t baudrate)
{
	return uart_init(USART3, baudrate);
}

/**		  
 * @brief	 读取串口一字节数据，如果无数据则阻塞  
 * @param byte
 * @return 0-成功，-1-错误，-2-超时，-3-无数据  
 */
int8_t xbee_serial_read_onebyte(uint8_t *byte, int32_t timeout)
{
	return serial_read_onebyte(&uart3_obj, byte, timeout);
}

void xbee_clear_buffer(void)
{
	UINT8 ch = 0;
	INT16 ret = 0;
	
	while ((ret = xbee_serial_read_onebyte(&ch, SYSTEM_TICKS_PER_SEC)) != -2)
	{
		if (0 == ret)
		{
			DEBUG_PRINT(DEBUG_WARN, "xbee_clear_buffer recv 0x%02X\n", ch);
		}
		else
		{
			DEBUG_PRINT(DEBUG_ERROR, "xbee_clear_buffer ret=%d\n", ret);
		}
	}
	return;
}

void xbee_serial_send_onebyte (uint8_t byte)
{
	serial_send_onebyte(USART3, &uart3_obj, byte);
    return;
}

/*************************************************************
调试打印串口相关函数
**************************************************************/
int debug_uart_init(uint32_t baudrate)
{
	return uart_init(USART1, baudrate);
}

int8_t debug_serial_read_onebyte(uint8_t *byte)
{
	return serial_read_onebyte(&uart1_obj, byte, 0);
}

/**
 * \b uart_putchar
 *
 * Write a char out via UART1
 *
 * @param[in] c Character to send
 *
 * @return Character sent
 */
FORCE_USED char uart_putchar (char c)
{
    /* Block on private access to the UART */
    if (atomMutexGet(&uart1_obj.mutex, 0) == ATOM_OK)
    {
        /* Convert \n to \r\n */
        if (c == '\n')
            putchar('\r');

        /* Write a character to the USART1 */
         USART_SendData8(USART1, c);
        /* Loop until the end of transmission */
        while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);


        /* Return mutex access */
        atomMutexPut(&uart1_obj.mutex);

    }

    return (c);
}


/* COSMIC: Requires putchar() routine to override stdio */
#if defined(__CSMC__)
/**
 * \b putchar
 *
 * Retarget putchar() to use UART1
 *
 * @param[in] c Character to send
 *
 * @return Character sent
 */
char putchar (char c)
{
    return (uart_putchar(c));
}
#endif /* __CSMC__ */


/* RAISONANCE: Requires putchar() routine to override stdio */
#if defined(__RCSTM8__)
/**
 * \b putchar
 *
 * Retarget putchar() to use UART1
 *
 * @param[in] c Character to send
 *
 * @return 1 on success
 */
int putchar (char c)
{
    uart_putchar(c);
    return (1);
}
#endif /* __RCSTM8__ */


/* IAR: Requires __write() routine to override stdio */
#if defined(__IAR_SYSTEMS_ICC__)
/**
 * \b __write
 *
 * Override for IAR stream output
 *
 * @param[in] handle Stdio handle. -1 to flush.
 * @param[in] buf Pointer to buffer to be written
 * @param[in] bufSize Number of characters to be written
 *
 * @return Number of characters sent
 */
FORCE_USED size_t __write(int handle, const unsigned char *buf, size_t bufSize)
{
    size_t chars_written = 0;
    
    /* Ignore flushes */
    if (handle == -1)
    {
      chars_written = (size_t)0; 
    }
    /* Only allow stdout/stderr output */
    else if ((handle != 1) && (handle != 2))
    {
      chars_written = (size_t)-1; 
    }
    /* Parameters OK, call the low-level character output routine */
    else
    {
        while (chars_written < bufSize)
        {
            uart_putchar (buf[chars_written]);
            chars_written++;
        }
    }
    
    return (chars_written);
}
#endif /* __IAR_SYSTEMS_ICC__ */
