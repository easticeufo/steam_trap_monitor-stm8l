      
/**@file 
 * @note tiansixinxi. All Right Reserved.
 * @brief  
 * 
 * @author   madongfang
 * @date     2016-7-22
 * @version  V1.0.0
 * 
 * @note ///Description here 
 * @note History:        
 * @note     <author>   <time>    <version >   <desc>
 * @note  
 * @warning  
 */

#include "base_fun.h"
#include <atom.h>
#include "uart.h"
#include "xbee.h"

#define MAX_CMD_LEN 64
#define SHELL_PROMPT "~# "

#define BOARD_GPRS 1
#define BOARD_XBEE 2

extern void get_rtc_time(INT8 * time_str,INT16 buff_len);
extern void set_rtc_time(INT8 *time_str);
extern void gprs_pwrkey(INT16 value);
extern void gprs_pwd_en(INT16 value);
extern void set_prt_gprs_data(INT16 value);
extern INT16 sync_ntp_time(void);
extern INT16 get_slave_board_type(void);
extern UINT16 get_adc_value(ADC_Channel_TypeDef adc_channel);
extern UINT16 get_pressure(void);

typedef struct
{
	INT8 *p_cmd;
	void (*func)(void);
}SHELL_CMD_FUNC;

static void list_all_cmd(void);
static void set_debug(void);
static void set_date(void);
static void sync_ntp(void);
static void enter_halt(void);
static void at(void);
static void send(void);
static void set_pwrkey(void);
static void prt_gprs(void);
static void test(void);

static INT8 shell_line[MAX_CMD_LEN] = {0};
static INT8 line_position = 0;

static SHELL_CMD_FUNC cmd_fun[] = 
{
	{"list", list_all_cmd},
	{"setDebug", set_debug},
	{"date", set_date},
	{"syncNtp", sync_ntp},
	{"halt", enter_halt},
	{"at", at},
	{"AT", at},
	{"send", send},
	{"setPwr", set_pwrkey},
	{"prtGprs", prt_gprs},
	{"test", test},
	{NULL, NULL}
};

static void list_all_cmd(void)
{
	INT8 i = 0;

	printf("support commond:\n");
	
	for (i = 0; cmd_fun[i].p_cmd != NULL; i++)
	{
		printf(cmd_fun[i].p_cmd);
		printf("\n");
	}
	
	return;
}

static void set_debug(void)
{
	INT8 cmd[MAX_CMD_LEN] = {0};
	INT8 param[8] = {0};
	INT32 level = 0;
	
	if (sscanf(shell_line, "%s%s", cmd, param) == 2)
	{
		level = atoi(param);
		
		set_debug_level(level);
	}

	printf("debug level: %ld\n", get_debug_level());
	
	return;
}

static void set_date(void)
{
	INT8 cmd[MAX_CMD_LEN] = {0};
	INT8 date_str[16] = {0};
	INT8 time_str[16] = {0};
	
	if (sscanf(shell_line, "%s%s%s", cmd, date_str, time_str) == 3)
	{
		snprintf(cmd, sizeof(cmd), "%s %s", date_str, time_str);
		set_rtc_time(cmd);
	}

	get_rtc_time(cmd, sizeof(cmd));

	printf("%s\n", cmd);
	
	return;
}

static void sync_ntp(void)
{
	INT8 str[32] = {0};

	sync_ntp_time();
	
	get_rtc_time(str, sizeof(str));
	printf("%s\n", str);
	
	return;
}

void enter_halt(void)
{
	halt();
}

static void at(void)
{
	INT8 cmd[4] = {0};
	INT8 param[64] = {0};
	INT16 i = 0;

	sscanf(shell_line, "%s%s", cmd, param);
	
	gprs_serial_send_onebyte((UINT8)'A');
	gprs_serial_send_onebyte((UINT8)'T');
	for (i = 0; param[i] != '\0'; i++)
	{
		gprs_serial_send_onebyte((UINT8)param[i]);
	}
	gprs_serial_send_onebyte((UINT8)'\r');
}

static void send(void)
{
	INT8 cmd[4] = {0};
	INT8 param[16] = {0};
	INT16 i = 0;

	sscanf(shell_line, "%s%s", cmd, param);
	
	for (i = 0; param[i] != '\0'; i++)
	{
		gprs_serial_send_onebyte((UINT8)param[i]);
	}
}

static void set_pwrkey(void)
{
	INT8 cmd[16] = {0};
	INT16 value = 0;

	sscanf(shell_line, "%s%d", cmd, &value);

	if (get_slave_board_type() == BOARD_GPRS)
	{
		gprs_pwd_en(value);
		gprs_pwrkey(value);
	}
	else if (get_slave_board_type() == BOARD_XBEE)
	{
		if (0 == value)
		{
			xbee_sleep();
		}
		else
		{
			xbee_wakeup();
		}
	}
	
	return;
}

static void prt_gprs(void)
{
	INT8 cmd[16] = {0};
	INT16 value = 0;

	sscanf(shell_line, "%s%d", cmd, &value);

	set_prt_gprs_data(value);
	
	return;
}

void test(void)
{
	INT8 cmd[16] = {0};
	INT8 param[8] = {0};
	INT16 i = 0;
	
	if (sscanf(shell_line, "%s%s", cmd, param) == 2)
	{
		if (strcmp(param, "0") == 0)
		{
			GPIO_ToggleBits(GPIOD, GPIO_Pin_7); // 开关传感器电源
		}
		else if (strcmp(param, "1") == 0)
		{
			GPIO_ToggleBits(GPIOG, GPIO_Pin_7); // 开关外围模拟采样电路电源
		}
		else if (strcmp(param, "3") == 0)
		{
			printf("J3 test:\n");
			for (i = 0; i < 10; i++)
			{
				printf("current=%u\n", get_pressure());
				atomTimerDelay(SYSTEM_TICKS_PER_SEC);
			}
		}
		else if (strcmp(param, "5") == 0)
		{
			printf("J5 test:\n");
			for (i = 0; i < 1000; i++)
			{
				printf("ultrasound=%u\n", get_adc_value(ADC_Channel_2));
				atomTimerDelay(SYSTEM_TICKS_PER_SEC / 100);
			}
		}
		else if (strcmp(param, "9") == 0)
		{
			printf("J9 test:\n");
			for (i = 0; i < 10; i++)
			{
				printf("voltage=%u\n", get_adc_value(ADC_Channel_1));
				atomTimerDelay(SYSTEM_TICKS_PER_SEC);
			}
		}
		else
		{
			printf("unknown test parameter\n");
		}
		
	}

	return;
}

void shell_cmd_run(void)
{
	INT8 i = 0;
	INT8 cmd[MAX_CMD_LEN] = {0};

	memset(cmd, 0, sizeof(cmd));
	if (sscanf(shell_line, "%s", cmd) != 1)
	{
		printf("shell commond error\n");
	}
	
	for (i = 0; cmd_fun[i].p_cmd != NULL; i++)
	{
		if (strcmp(cmd, cmd_fun[i].p_cmd) == 0)
		{
			cmd_fun[i].func();
			return;
		}
	}

	printf("unknown commond:cmd=%s,len=%d\n", cmd, strlen(cmd));
	list_all_cmd();
	return;
}

/**		  
 * @brief	 自定义shell功能，该函数永远不返回
 * @param 无
 * @return 无	  
 */
void shell(void)
{
	INT8 ch;

	printf(SHELL_PROMPT);
	
	FOREVER
	{
		if (debug_serial_read_onebyte((UINT8 *)&ch) != 0)
		{
			DEBUG_PRINT(DEBUG_ERROR, "serial_read_onebyte error\n");
			continue;
		}

		if (ch == 0x08) // 退格键
		{
			if (line_position != 0)
			{
				printf("%c %c", ch, ch);
			}

			if (line_position > 0)
			{
				line_position--;
			}

			shell_line[line_position] = 0;
			continue;
		}

		if (ch == '\r')
		{
			shell_line[line_position] = 0;
			printf("\n");
			if (line_position > 0)
			{
				shell_cmd_run();
				memset(shell_line, 0, sizeof(shell_line));
				line_position = 0;
				printf("\n");
			}
			printf(SHELL_PROMPT);
			continue;
		}

		if (line_position < MAX_CMD_LEN - 1)
		{
			shell_line[line_position] = ch;
			line_position++;
			printf("%c", ch);
		}
	}
}

