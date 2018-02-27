/*
 * Copyright (c) 2010, Kelvin Lawson. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. No personal names or organizations' names associated with the
 *    Atomthreads project may be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE ATOMTHREADS PROJECT AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright (c) 2013 Wei Shuai <cpuwolf@gmail.com>
 *     Modify to adapt STM8L
 *
 * Code is designed for
 *     STM8L mini system board
 *     get one from http://cpuwolf.taobao.com
 *
 * STM8L mini system board:
 * PB0: on-board LED
 *
 */

#include "base_fun.h"
#include <atom.h>
#include <atomtimer.h>
#include <stm8l15x.h>
#include "atomport-private.h"
#include "uart.h"
#include "gprs.h"
#include "mqtt.h"
#include "xbee.h"

/* Constants */

/*
 * Idle thread stack size
 *
 * This needs to be large enough to handle any interrupt handlers
 * and callbacks called by interrupt handlers (e.g. user-created
 * timer callbacks) as well as the saving of all context when
 * switching away from this thread.
 *
 * In this case, the idle stack is allocated on the BSS via the
 * idle_thread_stack[] byte array.
 */
#define IDLE_STACK_SIZE_BYTES       128


/*
 * Main thread stack size
 *
 * Note that this is not a required OS kernel thread - you will replace
 * this with your own application thread.
 *
 * In this case the Main thread is responsible for calling out to the
 * test routines. Once a test routine has finished, the test status is
 * printed out on the UART and the thread remains running in a loop
 * flashing a LED.
 *
 * The Main thread stack generally needs to be larger than the idle
 * thread stack, as not only does it need to store interrupt handler
 * stack saves and context switch saves, but the application main thread
 * will generally be carrying out more nested function calls and require
 * stack for application code local variables etc.
 *
 * With all OS tests implemented to date on the STM8, the Main thread
 * stack has not exceeded 256 bytes. To allow all tests to run we set
 * a minimum main thread stack size of 204 bytes. This may increase in
 * future as the codebase changes but for the time being is enough to
 * cope with all of the automated tests.
 */
#define MAIN_STACK_SIZE_BYTES       400

#define MQTT_NODE_TO_SERVER_TOPIC "NodeToServer03"
#define MQTT_SERVER_TO_NODE_TOPIC "ServerToNode03"


/*
 * Startup code stack
 *
 * Some stack space is required at initial startup for running the main()
 * routine. This stack space is only temporarily required at first bootup
 * and is no longer required as soon as the OS is started. By default
 * Cosmic sets this to the top of RAM and it grows down from there.
 *
 * Because we only need this temporarily you may reuse the area once the
 * OS is started, and are free to use some area other than the top of RAM.
 * For convenience we just use the default region here.
 */


/* Linker-provided startup stack location (usually top of RAM) */
extern int _stack;

extern void shell(void);
extern void gprs_debug_thread(UINT32 param);

/* Application threads' TCBs */
static ATOM_TCB main_tcb;

/* Main thread's stack area (large so place outside of the small page0 area on STM8) */
NEAR static uint8_t main_thread_stack[MAIN_STACK_SIZE_BYTES];

/* Idle thread's stack area (large so place outside of the small page0 area on STM8) */
NEAR static uint8_t idle_thread_stack[IDLE_STACK_SIZE_BYTES];

// GPRS线程控制块信息
#define GPRS_STACK_SIZE_BYTES       700
static ATOM_TCB gprs_tcb;
NEAR static uint8_t gprs_thread_stack[GPRS_STACK_SIZE_BYTES];

// GPRS调试线程控制块信息
#define GPRS_DEBUG_STACK_SIZE_BYTES       256
static ATOM_TCB gprs_debug_tcb;
NEAR static uint8_t gprs_debug_thread_stack[GPRS_DEBUG_STACK_SIZE_BYTES];

INT8 g_unique_id[24] = {0};

#define BOARD_GPRS 1
#define BOARD_XBEE 2
static INT16 slave_board_type = 0; // 主板挂在的从板类型
	
#define DEFAULT_UPDATE_INTERVAL 10 ///< 默认使用GPRS连接internet更新一次命令并进行NTP校时的间隔
static UINT16 rtc_it_count = DEFAULT_UPDATE_INTERVAL; // RTC唤醒中断计数，每隔30s增加一次

typedef struct
{
  INT8 hour;
  INT8 min;
  INT8 sec;
}
TIME;

INT16 get_slave_board_type(void)
{
	return slave_board_type;
}

/**
  * \b  time1 - time2，返回两个时间相差的秒数
  */
static INT32 sub_time(TIME time1, TIME time2)
{
	INT32 sec = 0;

	sec = (INT32)(time1.hour - time2.hour) * 3600;
	sec += (time1.min - time2.min) * 60;
	sec += (time1.sec - time2.sec);
	return sec;
}

static INT16 set_time(TIME *p_time, const INT8 *p_time_str)
{
	INT16 hour = 0;
	INT16 min = 0;
	INT16 sec = 0;
	
	if (NULL == p_time || NULL == p_time_str)
	{
		DEBUG_PRINT(DEBUG_ERROR, "param error!\n");
		return -1;
	}
	
	if (sscanf(p_time_str, "%d:%d:%d", &hour, &min, &sec) != 3)
	{
		DEBUG_PRINT(DEBUG_ERROR, "time string format error, p_time_str=%s\n", p_time_str);
		return -1;
	}

	if (hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59)
	{
		DEBUG_PRINT(DEBUG_ERROR, "time error\n");
		return -1;
	}

	p_time->hour = (INT8)hour;
	p_time->min = (INT8)min;
	p_time->sec = (INT8)sec;

	return 0;
}

static void get_time_now(TIME *p_time)
{
	RTC_TimeTypeDef   RTC_TimeStr;
	
	/* Wait until the calendar is synchronized */
	while (RTC_WaitForSynchro() != SUCCESS);
	/* Get the current Time*/
	RTC_GetTime(RTC_Format_BIN, &RTC_TimeStr);

	p_time->hour = (INT8)RTC_TimeStr.RTC_Hours;
	p_time->min = (INT8)RTC_TimeStr.RTC_Minutes;
	p_time->sec = (INT8)RTC_TimeStr.RTC_Seconds;

	return;
}

static INT16 get_str_from_json(const INT8 *p_json, const INT8 *p_tag, INT8 *p_buff)
{
	INT8 *ptr = NULL;
	
	if (NULL == p_json || NULL == p_tag || NULL == p_buff)
	{
		DEBUG_PRINT(DEBUG_ERROR, "param error!\n");
		return -1;
	}

	ptr = strstr(p_json, p_tag);

	if (NULL == ptr)
	{
		DEBUG_PRINT(DEBUG_WARN, "can not find tag %s\n", p_tag);
		return -1;
	}
	if (sscanf(ptr + strlen(p_tag), "\":\"%[^\"]", p_buff) != 1)
	{
		DEBUG_PRINT(DEBUG_WARN, "data format error!\n");
		return -1;
	}

	return 0;
}

static INT16 get_int_from_json(const INT8 *p_json, const INT8 *p_tag, INT16 *p_value)
{
	INT8 *ptr = NULL;
	
	if (NULL == p_json || NULL == p_tag || NULL == p_value)
	{
		DEBUG_PRINT(DEBUG_ERROR, "param error!\n");
		return -1;
	}

	ptr = strstr(p_json, p_tag);

	if (NULL == ptr)
	{
		DEBUG_PRINT(DEBUG_WARN, "can not find tag %s\n", p_tag);
		return -1;
	}
	if (sscanf(ptr + strlen(p_tag), "\":%d", p_value) != 1)
	{
		DEBUG_PRINT(DEBUG_WARN, "data format error!\n");
		return -1;
	}

	return 0;
}

void add_rtc_it_count(void)
{
	rtc_it_count++;
}

/**
  * \b  Configure peripherals Clock
  * STM8L peripherals clock are disabled by default
  * @param  None
  * @retval None
  */
static void CLK_Config(void)
{
	/*High speed internal clock prescaler: 1*/
	CLK_SYSCLKDivConfig(CLK_SYSCLKDiv_1);
    /* Enable TIM1 clock */
    CLK_PeripheralClockConfig(CLK_Peripheral_TIM1, ENABLE);
    /* Enable TIM2 clock */
    CLK_PeripheralClockConfig(CLK_Peripheral_TIM2, ENABLE);
    /* Enable USART clock */
    CLK_PeripheralClockConfig(CLK_Peripheral_USART1, ENABLE);
	CLK_PeripheralClockConfig(CLK_Peripheral_USART2, ENABLE);
	/* Enable ADC1 clock */
	CLK_PeripheralClockConfig(CLK_Peripheral_ADC1, ENABLE);
}

/**
  * \b  Configure GPIOs
  * @param  None
  * @retval None
  */
static void GPIO_Config(void)
{
    /* Configure GPIO for flashing STM8L mini system board GPIO B0 */
	GPIO_DeInit(GPIOD);
	GPIO_Init(GPIOD, GPIO_Pin_7, GPIO_Mode_Out_PP_Low_Fast); // 5V电源开关
	
    GPIO_DeInit(GPIOB);
	GPIO_Init(GPIOB, GPIO_Pin_0, GPIO_Mode_Out_PP_Low_Fast); // GPRS模块电源开关
    GPIO_Init(GPIOB, GPIO_Pin_1, GPIO_Mode_Out_PP_Low_Fast); // zigbee模块电源、gprs模块开关机
	GPIO_Init(GPIOB, GPIO_Pin_2, GPIO_Mode_Out_PP_Low_Fast); // zigbee模块RESET、gprs模块EMERGOFF
	GPIO_Init(GPIOB, GPIO_Pin_3, GPIO_Mode_Out_PP_Low_Fast); // zigbee模块RTS、gprs模块RTS
	GPIO_Init(GPIOB, GPIO_Pin_5, GPIO_Mode_Out_PP_Low_Fast); // zigbee模块DTR/SLEEP_RQ、gprs模块DTR
	GPIO_Init(GPIOB, GPIO_Pin_7, GPIO_Mode_In_FL_No_IT); // zigbee模块ON/SLEEP

	GPIO_DeInit(GPIOG);
	GPIO_Init(GPIOG, GPIO_Pin_7, GPIO_Mode_Out_PP_Low_Fast); // 模拟采样电路电源

	GPIO_ResetBits(GPIOG, GPIO_Pin_7); // 关闭模拟采样电路电源
	GPIO_ResetBits(GPIOD, GPIO_Pin_7); // 关闭22V电源
	GPIO_ResetBits(GPIOB, GPIO_Pin_0); // 关闭GPRS模块电源
	GPIO_ResetBits(GPIOB, GPIO_Pin_1); // Xbee关闭，GPRS模块开机关闭
	GPIO_ResetBits(GPIOB, GPIO_Pin_2); // 关闭EMERGOFF
	GPIO_SetBits(GPIOB, GPIO_Pin_3); // RTS拉高，防止流控生效
	GPIO_SetBits(GPIOB, GPIO_Pin_5); // DTR设置成高电平，防止XBEE启动时进入Bootloader

	/* UART1 */
    /* Configure USART Tx as alternate function push-pull  (software pull up)*/
    GPIO_ExternalPullUpConfig(GPIOC, GPIO_Pin_3, ENABLE);
    /* Configure USART Rx as alternate function push-pull  (software pull up)*/
    GPIO_ExternalPullUpConfig(GPIOC, GPIO_Pin_2, DISABLE);

	/* UART2 */
    GPIO_ExternalPullUpConfig(GPIOE, GPIO_Pin_4, ENABLE);
    GPIO_ExternalPullUpConfig(GPIOE, GPIO_Pin_3, DISABLE);
}

/**
  * @brief  Configure RTC peripheral
  * @param  None
  * @retval None
  */
static void RTC_Config(void)
{
	RTC_InitTypeDef   RTC_InitStr;

	/* Select LSI (38KHz) as RTC clock source */
	CLK_RTCClockConfig(CLK_RTCCLKSource_LSI, CLK_RTCCLKDiv_1);

	CLK_PeripheralClockConfig(CLK_Peripheral_RTC, ENABLE);

	RTC_InitStr.RTC_HourFormat = RTC_HourFormat_24;
	RTC_InitStr.RTC_AsynchPrediv = 0x5E;
	RTC_InitStr.RTC_SynchPrediv = 0x018F;
	RTC_Init(&RTC_InitStr);

	RTC_WakeUpCmd(DISABLE);
	/* Configures the RTC wakeup timer_step = ck_spre = 1s */
    RTC_WakeUpClockConfig(RTC_WakeUpClock_CK_SPRE_16bits);

    /* Enable wake up unit Interrupt */
    RTC_ITConfig(RTC_IT_WUT, ENABLE);

	/* RTC wake-up event every 30s (timer_step x (29 + 1) )*/
    RTC_SetWakeUpCounter(29);
	
    RTC_WakeUpCmd(ENABLE);

}

/**
  * @brief  Configure ADC peripheral
  * @param  None
  * @retval None
  */
static void ADC_Config(void)
{
  /* Initialise and configure ADC1 */
  ADC_Init(ADC1, ADC_ConversionMode_Single, ADC_Resolution_12Bit, ADC_Prescaler_2);
  ADC_SamplingTimeConfig(ADC1, ADC_Group_SlowChannels, ADC_SamplingTime_384Cycles);

  /* Enable ADC1 */
  ADC_Cmd(ADC1, ENABLE);
}

void get_rtc_time(INT8 *time_str, INT16 buff_len)
{
	RTC_TimeTypeDef   RTC_TimeStr;
	RTC_DateTypeDef   RTC_DateStr;
	
	/* Wait until the calendar is synchronized */
	while (RTC_WaitForSynchro() != SUCCESS);

	/* Get the current Time*/
	RTC_GetTime(RTC_Format_BIN, &RTC_TimeStr);

	/* Wait until the calendar is synchronized */
	while (RTC_WaitForSynchro() != SUCCESS);

	RTC_GetDate(RTC_Format_BIN, &RTC_DateStr);

	snprintf(time_str, buff_len, "20%02d-%02d-%02d %02d:%02d:%02d"
		, RTC_DateStr.RTC_Year, RTC_DateStr.RTC_Month, RTC_DateStr.RTC_Date
		, RTC_TimeStr.RTC_Hours, RTC_TimeStr.RTC_Minutes, RTC_TimeStr.RTC_Seconds);

	return;
}

void set_rtc_time(INT8 *time_str)
{
	RTC_TimeTypeDef   RTC_TimeStr;
	RTC_DateTypeDef   RTC_DateStr;
	INT16 year = 0, month = 1, day = 1, hour = 0, min = 0, sec = 0;
	INT8 zone_str[4] = {0};

	RTC_DateStructInit(&RTC_DateStr);
	RTC_TimeStructInit(&RTC_TimeStr);

	if (sscanf(time_str, "20%d-%d-%d %d:%d:%d%s"
		, &year, &month, &day, &hour, &min, &sec, zone_str) >= 6)
	{
		if (year < 0 || year > 99 || month < 1 || month > 12 || day < 1 || day > 31 
			|| hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59)
		{
			DEBUG_PRINT(DEBUG_ERROR, "time out of range!\n");
			return;
		}
		
		RTC_DateStr.RTC_Year = (UINT8)year;
		RTC_DateStr.RTC_Month = (RTC_Month_TypeDef)month;
		RTC_DateStr.RTC_Date = (UINT8)day;
		RTC_TimeStr.RTC_Hours = (UINT8)hour;
		RTC_TimeStr.RTC_Minutes = (UINT8)min;
		RTC_TimeStr.RTC_Seconds = (UINT8)sec;
		
		RTC_SetDate(RTC_Format_BIN, &RTC_DateStr);
		RTC_SetTime(RTC_Format_BIN, &RTC_TimeStr);
	}
	else
	{
		DEBUG_PRINT(DEBUG_ERROR, "time format error!\n");
	}

	return;
}

INT16 sync_ntp_time(void)
{
	INT8 str[32] = {0};

	if (gprs_get_ntp_time(str, sizeof(str)) != 0)
	{
		DEBUG_PRINT(DEBUG_ERROR, "gprs_get_ntp_time error!\n");
		return -1;
	}

	set_rtc_time(str);
	return 0;
}

UINT16 get_adc_value(ADC_Channel_TypeDef adc_channel)
{
	UINT16 value = 0;
	
	ADC_ChannelCmd(ADC1, adc_channel, ENABLE);
	ADC_SoftwareStartConv(ADC1);
	while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == 0); // Wait until End-Of-Convertion
	value = ADC_GetConversionValue(ADC1);
	ADC_ChannelCmd(ADC1, adc_channel, DISABLE);
	
	return value;
}

UINT16 get_pressure(void)
{
	UINT32 tmp = 0;

	tmp = get_adc_value(ADC_Channel_0);
	tmp = tmp * 439 / 625 + 2;
	
	return (UINT16)tmp;
}

static void set_all_io_lowpower_mode(void)
{
	GPIO_DeInit(GPIOA);
	GPIO_DeInit(GPIOB);
	GPIO_DeInit(GPIOC);
	GPIO_DeInit(GPIOD);
	GPIO_DeInit(GPIOE);
	ADC_Cmd(ADC1, DISABLE);
}

void gprs_thread_func(UINT32 param)
{
	INT8 buff[100] = {0};
	INT8 time_str[20] = {0};
	INT8 id[24] = {0};
	INT8 start_time_str[10] = {0};
	INT8 stop_time_str[10] = {0};
	TIME time_now = {0,0,0};
	TIME start_time = {1, 0, 0}; // 开始采样时间
	TIME stop_time = {0, 0, 0}; // 停止采样时间
	INT16 interval = 0; // interval等于0时不采样
	INT16 update_interval = DEFAULT_UPDATE_INTERVAL;
	INT16 tmp = 0;
	UINT16 j5 = 0, j5_1 = 0, j5_2 = 0, j5_3 = 0;
	UINT16 j9 = 0, j9_1 = 0, j9_2 = 0, j9_3 = 0;
	INT16 i = 0;

	FOREVER
	{
		get_time_now(&time_now);
		DEBUG_PRINT(DEBUG_INFO, "time now: %02d:%02d:%02d\n", time_now.hour, time_now.min, time_now.sec);
		DEBUG_PRINT(DEBUG_INFO, "start time: %02d:%02d:%02d\n", start_time.hour, start_time.min, start_time.sec);
		DEBUG_PRINT(DEBUG_INFO, "stop time: %02d:%02d:%02d\n", stop_time.hour, stop_time.min, stop_time.sec);
		DEBUG_PRINT(DEBUG_INFO, "interval=%d\n", interval);
		DEBUG_PRINT(DEBUG_INFO, "update_interval=%d\n", update_interval);

		if (sub_time(time_now, start_time) > -61 && sub_time(time_now, stop_time) <= 0 && interval != 0)
		{
			if (gprs_init() != 0)
			{
				DEBUG_PRINT(DEBUG_ERROR, "gprs init failed!\n");
				goto sleep;
			}
			
			if (mqtt_connect(0) != 0)
			{
				DEBUG_PRINT(DEBUG_ERROR, "mqtt connect failed\n");
				goto sleep;
			}

			GPIO_SetBits(GPIOD, GPIO_Pin_7); // 打开传感器电源
			GPIO_SetBits(GPIOG, GPIO_Pin_7); // 打开模拟采样电路电源
			get_time_now(&time_now);
			while (sub_time(time_now, stop_time) <= 0) // 当连接上mqtt服务器后需要一直在此循环直到超过采样停止时间
			{
				if (sub_time(time_now, start_time) >= 0) // 等到开始时间后才开始正式采样
				{
					snprintf(buff, sizeof(buff), "{\"type\":\"sample\",\"id\":\"%s\",\"time\":\"%02d:%02d:%02d\",\"pressure\":%u}"
						, g_unique_id, time_now.hour, time_now.min, time_now.sec, get_pressure());
					if (mqtt_publish(MQTT_NODE_TO_SERVER_TOPIC, buff) != 0)
					{
						DEBUG_PRINT(DEBUG_ERROR, "mqtt_publish failed\n");
					}
					atomTimerDelay(SYSTEM_TICKS_PER_SEC * interval);
				}
				else
				{
					atomTimerDelay(SYSTEM_TICKS_PER_SEC / 5);
				}
				get_time_now(&time_now);
			}

			mqtt_disconnect();
		}
		else if (rtc_it_count >= update_interval) // 每隔(update_interval / 2)分钟，需要校时一次并且和mqtt服务器通信获取设置的参数
		{
			GPIO_Config();
			ADC_Config();
			
			if (gprs_init() != 0)
			{
				DEBUG_PRINT(DEBUG_ERROR, "gprs init failed!\n");
				rtc_it_count = 0;
				goto sleep;
			}

			sync_ntp_time();
			
			if (mqtt_connect(10) != 0)
			{
				DEBUG_PRINT(DEBUG_ERROR, "mqtt connect failed\n");
				rtc_it_count = 0;
				goto sleep;
			}

			/* 处理服务器下发的命令 */
			if (mqtt_subscrib(MQTT_SERVER_TO_NODE_TOPIC, buff, sizeof(buff)) == 0) 
			{
				DEBUG_PRINT(DEBUG_NOTICE, "mqtt subscrib buff=%s\n", buff);

				if (get_str_from_json(buff, "id", id) == 0 
					&& get_str_from_json(buff, "start", start_time_str) == 0 
					&& get_str_from_json(buff, "stop", stop_time_str) == 0)
				{
					if (strcmp(id, "all") == 0 || strcmp(id, g_unique_id) == 0)
					{
						if (set_time(&start_time, start_time_str) != 0)
						{
							DEBUG_PRINT(DEBUG_ERROR, "set start time failed\n");
						}
						if (set_time(&stop_time, stop_time_str) != 0)
						{
							DEBUG_PRINT(DEBUG_ERROR, "set stop time failed\n");
						}

						if (get_int_from_json(buff, "interval", &interval) == 0)
						{
							DEBUG_PRINT(DEBUG_NOTICE, "set interval=%d\n", interval);
						}

						if (get_int_from_json(buff, "update", &tmp) == 0 && tmp > 0)
						{
							DEBUG_PRINT(DEBUG_NOTICE, "set update=%d\n", tmp);
							update_interval = tmp * 2;
						}
					}
				}
				else
				{
					DEBUG_PRINT(DEBUG_ERROR, "mqtt subscrib data format error buff=%s\n", buff);
				}
			}
			else
			{
				DEBUG_PRINT(DEBUG_ERROR, "mqtt_subscrib failed\n");
			}

			GPIO_SetBits(GPIOD, GPIO_Pin_7); // 打开传感器电源
			GPIO_SetBits(GPIOG, GPIO_Pin_7); // 打开模拟采样电路电源
			atomTimerDelay(SYSTEM_TICKS_PER_SEC);
			
			/* 采样上传 */
			get_rtc_time(time_str, sizeof(time_str));
			/* 电压采样取三次平均值 */
			j9_1 = get_adc_value(ADC_Channel_1);
			j9_2 = get_adc_value(ADC_Channel_1);
			j9_3 = get_adc_value(ADC_Channel_1);
			j9 = (j9_1 + j9_2 + j9_3) / 3;
			
			/* 超声波采样取前三个最大值 */
			j5_1 = j5_2 = j5_3 = 0;
			for (i = 0; i < 200; i++)
			{
				j5 = get_adc_value(ADC_Channel_2);
				if (j5 > j5_1)
				{
					j5_3 = j5_2;
					j5_2 = j5_1;
					j5_1 = j5;
				}
				else if (j5 > j5_2)
				{
					j5_3 = j5_2;
					j5_2 = j5;
				}
				else if (j5 > j5_3)
				{
					j5_3 = j5;
				}
				atomTimerDelay(SYSTEM_TICKS_PER_SEC / 100);
			}
			j5 = (j5_1 + j5_2 + j5_3) / 3; // 求平均

			GPIO_ResetBits(GPIOG, GPIO_Pin_7); // 关闭模拟采样电路电源
			GPIO_ResetBits(GPIOD, GPIO_Pin_7); // 关闭传感器电源
			
			snprintf(buff, sizeof(buff), "{\"type\":\"sample\",\"id\":\"%s\",\"time\":\"%s\",\"j5\":%u,\"j9\":%u}"
				, g_unique_id, time_str, j5, j9);
			if (mqtt_publish(MQTT_NODE_TO_SERVER_TOPIC, buff) != 0)
			{
				DEBUG_PRINT(DEBUG_ERROR, "mqtt_publish failed\n");
			}

			/* 关闭mqtt连接 */
			mqtt_disconnect();

			rtc_it_count = 0;
		}

sleep:	
		DEBUG_PRINT(DEBUG_NOTICE, "start halt\n");
		gprs_shutdown(); // GPRS模块关机
		set_all_io_lowpower_mode();
		halt();
		atomTimerDelay(SYSTEM_TICKS_PER_SEC);
		DEBUG_PRINT(DEBUG_NOTICE, "stop halt\n");
	}
}

INT16 sync_xbee_time(void)
{
	INT8 str[32] = {0};

	if (xbee_get_time(str, sizeof(str)) != 0)
	{
		DEBUG_PRINT(DEBUG_ERROR, "gprs_get_ntp_time error!\n");
		return -1;
	}

	set_rtc_time(str);
	return 0;
}

void xbee_thread_func(UINT32 param)
{
	INT8 buff[100] = {0};
	INT8 time_str[20] = {0};
	INT8 id[24] = {0};
	INT8 start_time_str[10] = {0};
	INT8 stop_time_str[10] = {0};
	TIME time_now = {0,0,0};
	TIME start_time = {1, 0, 0}; // 开始采样时间
	TIME stop_time = {0, 0, 0}; // 停止采样时间
	INT16 interval = 0; // interval等于0时不采样
	INT16 update_interval = DEFAULT_UPDATE_INTERVAL;
	INT16 tmp = 0;
	INT16 len = 0;

	GPIO_SetBits(GPIOB, GPIO_Pin_2); // Reset引脚拉高
	atomTimerDelay(SYSTEM_TICKS_PER_SEC * 4);

	while (xbee_init() != 0)
	{
		DEBUG_PRINT(DEBUG_ERROR, "xbee_init failed\n");
		atomTimerDelay(SYSTEM_TICKS_PER_SEC * 4);
	}

	atomTimerDelay(SYSTEM_TICKS_PER_SEC);
	
	DEBUG_PRINT(DEBUG_INFO, "xbee thread start work\n");
	FOREVER
	{
		get_time_now(&time_now);
		DEBUG_PRINT(DEBUG_INFO, "time now: %02d:%02d:%02d\n", time_now.hour, time_now.min, time_now.sec);
		DEBUG_PRINT(DEBUG_INFO, "start time: %02d:%02d:%02d\n", start_time.hour, start_time.min, start_time.sec);
		DEBUG_PRINT(DEBUG_INFO, "stop time: %02d:%02d:%02d\n", stop_time.hour, stop_time.min, stop_time.sec);
		DEBUG_PRINT(DEBUG_INFO, "interval=%d\n", interval);
		DEBUG_PRINT(DEBUG_INFO, "update_interval=%d\n", update_interval);

		if (sub_time(time_now, start_time) > -61 && sub_time(time_now, stop_time) <= 0 && interval != 0)
		{
			xbee_wakeup();
			atomTimerDelay(SYSTEM_TICKS_PER_SEC * 15); // 唤醒后需要等待XBEE寻找到zigbee网络后再发送数据

			GPIO_SetBits(GPIOD, GPIO_Pin_7); // 打开传感器电源
			get_time_now(&time_now);
			while (sub_time(time_now, stop_time) <= 0) // 当连接上mqtt服务器后需要一直在此循环直到超过采样停止时间
			{
				if (sub_time(time_now, start_time) >= 0) // 等到开始时间后才开始正式采样
				{
					len = snprintf(buff, sizeof(buff), "publish:{\"type\":\"sample\",\"id\":\"%s\",\"time\":\"%02d:%02d:%02d\",\"pressure\":%u}"
						, g_unique_id, time_now.hour, time_now.min, time_now.sec, get_pressure());
					xbee_write(buff, len);
					atomTimerDelay(SYSTEM_TICKS_PER_SEC * interval);
				}
				else
				{
					atomTimerDelay(SYSTEM_TICKS_PER_SEC / 5);
				}
				get_time_now(&time_now);
			}
			GPIO_ResetBits(GPIOD, GPIO_Pin_7); // 关闭传感器电源
		}
		else if (rtc_it_count >= update_interval) // 每隔(update_interval / 2)分钟，需要校时一次并且和mqtt服务器通信获取设置的参数
		{
			xbee_wakeup();
			atomTimerDelay(SYSTEM_TICKS_PER_SEC * 15); // 唤醒后需要等待XBEE寻找到zigbee网络后再发送数据

			sync_xbee_time();
			get_rtc_time(time_str, sizeof(time_str));

			xbee_write("subscribe", strlen("subscribe"));
			if (xbee_recv_ret_str(buff, sizeof(buff), SYSTEM_TICKS_PER_SEC * 4) > 0) // 处理服务器下发的命令
			{
				DEBUG_PRINT(DEBUG_NOTICE, "mqtt subscrib buff=%s\n", buff);

				if (get_str_from_json(buff, "id", id) == 0 
					&& get_str_from_json(buff, "start", start_time_str) == 0 
					&& get_str_from_json(buff, "stop", stop_time_str) == 0)
				{
					if (strcmp(id, "all") == 0 || strcmp(id, g_unique_id) == 0)
					{
						if (set_time(&start_time, start_time_str) != 0)
						{
							DEBUG_PRINT(DEBUG_ERROR, "set start time failed\n");
						}
						if (set_time(&stop_time, stop_time_str) != 0)
						{
							DEBUG_PRINT(DEBUG_ERROR, "set stop time failed\n");
						}

						if (get_int_from_json(buff, "interval", &interval) == 0)
						{
							DEBUG_PRINT(DEBUG_NOTICE, "set interval=%d\n", interval);
						}

						if (get_int_from_json(buff, "update", &tmp) == 0 && tmp > 0)
						{
							DEBUG_PRINT(DEBUG_NOTICE, "set update=%d\n", tmp);
							update_interval = tmp * 2;
						}
					}
				}
				else
				{
					DEBUG_PRINT(DEBUG_ERROR, "mqtt subscrib data format error buff=%s\n", buff);
				}
			}
			else
			{
				DEBUG_PRINT(DEBUG_ERROR, "mqtt_subscrib failed\n");
			}

			len = snprintf(buff, sizeof(buff), "publish:{\"type\":\"alive\",\"id\":\"%s\",\"node_time\":\"%s\"}", g_unique_id, time_str);
			xbee_write(buff, len);
			
			rtc_it_count = 0;
		}
		else
		{
			DEBUG_PRINT(DEBUG_NOTICE, "start halt\n");
			xbee_sleep();
			halt();
			atomTimerDelay(SYSTEM_TICKS_PER_SEC);
			DEBUG_PRINT(DEBUG_NOTICE, "stop halt\n");
		}
	}
}

/**
 * \b main_thread_func
 * 创建其他工作线程，执行shell功能
 *
 * @param[in] param Unused (optional thread entry parameter)
 *
 * @return None
 */
static void main_thread_func (uint32_t param)
{
	INT8 build_date[16] = {0};

	/* Initialise UART (115200bps) */
    if (debug_uart_init(115200) != 0)
    {
        /* Error initialising UART */
		DEBUG_PRINT(DEBUG_ERROR, "initialising UART error!\n");
    }

	get_build_date(build_date, sizeof(build_date));
	printf("Steam Trap Monitor(V4.0): %s\n", build_date); // 打印程序版本

	slave_board_type = BOARD_GPRS;
	
	if (BOARD_GPRS == slave_board_type)
	{
		printf("slave board type: GPRS\n");
	}
	else if (BOARD_XBEE == slave_board_type)
	{
		printf("slave board type: XBEE\n");
	}
	else
	{
		printf("slave board type: UNKNOWN\n");
	}

	if (BOARD_GPRS == slave_board_type)
	{
		if (gprs_uart_init(115200) != 0)
	    {
	        /* Error initialising UART */
			DEBUG_PRINT(DEBUG_ERROR, "initialising UART error!\n");
	    }

		if (atomThreadCreate(&gprs_tcb, 20, gprs_thread_func, 0
			, &gprs_thread_stack[GPRS_STACK_SIZE_BYTES - 1], GPRS_STACK_SIZE_BYTES) != ATOM_OK)
		{
			DEBUG_PRINT(DEBUG_ERROR, "atomThreadCreate gprs_thread_func failed!\n");
		}
	}
	else if (BOARD_XBEE == slave_board_type)
	{
		if (gprs_uart_init(9600) != 0)
	    {
	        /* Error initialising UART */
			DEBUG_PRINT(DEBUG_ERROR, "initialising UART error!\n");
	    }

		if (atomThreadCreate(&gprs_tcb, 20, xbee_thread_func, 0
			, &gprs_thread_stack[GPRS_STACK_SIZE_BYTES - 1], GPRS_STACK_SIZE_BYTES) != ATOM_OK)
		{
			DEBUG_PRINT(DEBUG_ERROR, "atomThreadCreate xbee_thread_func failed!\n");
		}
	}
	else
	{
		DEBUG_PRINT(DEBUG_ERROR, "unknown slave board type! slave_board_type=%d\n", slave_board_type);
	}

	// 调试线程，用于串口输出gprs或xbee模块接收到的数据
	if (atomThreadCreate(&gprs_debug_tcb, 200, gprs_debug_thread, 0
		, &gprs_debug_thread_stack[GPRS_DEBUG_STACK_SIZE_BYTES - 1], GPRS_DEBUG_STACK_SIZE_BYTES) != ATOM_OK)
	{
		DEBUG_PRINT(DEBUG_ERROR, "atomThreadCreate gprs_debug_thread failed!\n");
	}

	shell(); // 启动shell，这个函数不会返回

	return;
}

/**
 * \b main
 *
 * Program entry point.
 *
 * Sets up the STM8 hardware resources (system tick timer interrupt) necessary
 * for the OS to be started. Creates an application thread and starts the OS.
 *
 * If the compiler supports it, stack space can be saved by preventing
 * the function from saving registers on entry. This is because we
 * are called directly by the C startup assembler, and know that we will
 * never return from here. The NO_REG_SAVE macro is used to denote such
 * functions in a compiler-agnostic way, though not all compilers support it.
 *
 */
NO_REG_SAVE void main ( void )
{
    int8_t status;

    /* CLK configuration */
    CLK_Config();
    /* GPIO configuration */
    GPIO_Config();
    /* RTC configuration */
    RTC_Config();

	ADC_Config();
	
    /**
     * Note: to protect OS structures and data during initialisation,
     * interrupts must remain disabled until the first thread
     * has been restored. They are reenabled at the very end of
     * the first thread restore, at which point it is safe for a
     * reschedule to take place.
     */

    /* Initialise the OS before creating our threads */
    status = atomOSInit(&idle_thread_stack[IDLE_STACK_SIZE_BYTES - 1], IDLE_STACK_SIZE_BYTES);
    if (status == ATOM_OK)
    {
        /* Enable the system tick timer */
        archInitSystemTickTimer();

        /* Create an application thread */
        status = atomThreadCreate(&main_tcb,
                                  100, main_thread_func, 0,
                                  &main_thread_stack[MAIN_STACK_SIZE_BYTES - 1],
                                  MAIN_STACK_SIZE_BYTES);
        if (status == ATOM_OK)
        {
            /**
             * First application thread successfully created. It is
             * now possible to start the OS. Execution will not return
             * from atomOSStart(), which will restore the context of
             * our application thread and start executing it.
             *
             * Note that interrupts are still disabled at this point.
             * They will be enabled as we restore and execute our first
             * thread in archFirstThreadRestore().
             */
            atomOSStart();
        }
    }

    /* There was an error starting the OS if we reach here */
    while (1)
    {
    }

}

