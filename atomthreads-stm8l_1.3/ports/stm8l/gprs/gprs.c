      
/**@file 
 * @note 
 * @brief  
 * 
 * @author   madongfang
 * @date     2015-5-19
 * @version  V1.0.0
 * 
 * @note ///Description here 
 * @note History:        
 * @note     <author>   <time>    <version >   <desc>
 * @note  
 * @warning  
 */

#include <atom.h>
#include <atomtimer.h>
#include <stm8l15x.h>
#include "uart.h"
#include "gprs.h"

#define GPRS_DEBUG DEBUG_PRINT
#define CENTER_ADDR "112.124.41.233" ///< mqtt服务器地址
#define CENTER_PORT 1883 ///< mqtt服务器端口


extern INT8 g_unique_id[24];

static INT16 gprs_is_connected = 0;
static INT16 gprs_is_on = 0; ///< GPRS模块是否开机 

static INT16 gprs_send_at(const INT8 *p_cmd)
{
	INT16 i = 0;

	if (NULL == p_cmd)
	{
		GPRS_DEBUG(DEBUG_ERROR, "param error\n");
		return -1;
	}

	gprs_serial_send_onebyte((UINT8)'A');
	gprs_serial_send_onebyte((UINT8)'T');
	for (i = 0; p_cmd[i] != '\0'; i++)
	{
		gprs_serial_send_onebyte((UINT8)p_cmd[i]);
	}
	gprs_serial_send_onebyte((UINT8)'\r');
	
	return 0;
}

/**		  
 * @brief		 接收gprs uart返回的一行数据(返回两个\r\n之间的数据)，无数据则阻塞
 * @param[out] p_buff 读出数据缓冲区
 * @param[in]  len 读出数据缓冲区大小
 * @param[in]  timeout 超时时间 (0 = forever)
 * @return	 成功返回获取到的字符串的长度(不包括\0)，失败返回负数，-2表示超时
 */
static INT16 gprs_recv_ret_line(INT8 *p_buff, INT16 len, INT32 timeout)
{
	INT16 i = 0;
	INT8 ch[2] = {0};
	INT16 ret = 0;

	if (NULL == p_buff)
	{
		GPRS_DEBUG(DEBUG_ERROR, "param error\n");
		return -1;
	}

	if ((ret = gprs_serial_read_onebyte((UINT8 *)&ch[0], timeout)) != 0)
	{
		GPRS_DEBUG(DEBUG_ERROR, "gprs_serial_read_onebyte error\n");
		return ret;
	}
	
	/* 接收起始字符 */
	FOREVER
	{
		if ((ret = gprs_serial_read_onebyte((UINT8 *)&ch[1], timeout)) != 0)
		{
			GPRS_DEBUG(DEBUG_ERROR, "gprs_serial_read_onebyte error\n");
			return ret;
		}

		if ('\r' == ch[0] && '\n' == ch[1])
		{
			break; // 找到起始字符
		}
		else
		{
			ch[0] = ch[1];
		}
	}
	
	/* 接收实际数据 */
	i = 0;
	while (i < len - 1)
	{
		if ((ret = gprs_serial_read_onebyte((UINT8 *)&p_buff[i], timeout)) != 0)
		{
			GPRS_DEBUG(DEBUG_ERROR, "gprs_serial_read_onebyte error\n");
			return ret;
		}
		
		if ('\r' == p_buff[i] || '>' == p_buff[i])
		{
			break;
		}
		i++;
	}
	ch[0] = p_buff[i];
	if ('>' == p_buff[i])
	{
		i++;
	}
	p_buff[i] = '\0';

	/* 接收结束字符 */
	FOREVER
	{
		if ((ret = gprs_serial_read_onebyte((UINT8 *)&ch[1], timeout)) != 0)
		{
			GPRS_DEBUG(DEBUG_ERROR, "gprs_serial_read_onebyte error\n");
			return ret;
		}

		if ('\r' == ch[0] && '\n' == ch[1])
		{
			break; // 找到结束字符
		}
		else if ('>' == ch[0] && ' ' == ch[1])
		{
			break;
		}
		else
		{
			ch[0] = ch[1];
		}
	}

	return i;
}

static INT16 gprs_process_recv_cmd(const INT8 *p_cmd)
{
	if (NULL == p_cmd)
	{
		GPRS_DEBUG(DEBUG_ERROR, "param error\n");
		return -1;
	}
	
	if (strcmp(p_cmd, "+CIPRXGET:1") == 0) // 收到数据
	{
		GPRS_DEBUG(DEBUG_NOTICE, "gprs receive data\n");
	}
	else if (strcmp(p_cmd, "CONNECTION CLOSED: 0") == 0) // TCP连接断开
	{
		gprs_is_connected = 0;
		GPRS_DEBUG(DEBUG_WARN, "gprs connection closed\n");
	}
	else if (strcmp(p_cmd, "OK") == 0)
	{
		GPRS_DEBUG(DEBUG_NOTICE, "gprs_process_recv_cmd recv OK\n");
	}
	else
	{
		GPRS_DEBUG(DEBUG_ERROR, "unknown cmd=%s\n", p_cmd);
	}
	
	return 0;
}

INT16 gprs_connect(void)
{
	INT8 cmd[48] = {0};
	INT8 str[32] = {0};
	INT16 ret = 0;

	/* 连接前先设置透传模式 */
	strcpy(cmd, "+CMMODE=1");
	gprs_send_at(cmd);
	FOREVER
	{
		if ((ret = gprs_recv_ret_line(str, sizeof(str), SYSTEM_TICKS_PER_SEC * 4)) < 0)
		{
			if (-2 == ret)
			{
				GPRS_DEBUG(DEBUG_ERROR, "gprs recv time out\n");
				return -2;
			}
			GPRS_DEBUG(DEBUG_ERROR, "gprs_recv_ret_line error, ret=%d\n", ret);
			continue;
		}
		if (strcmp(str, "OK") == 0)
		{
			break;
		}
		else
		{
			gprs_process_recv_cmd(str);
		}
	}
	atomTimerDelay(SYSTEM_TICKS_PER_SEC);

	/* 建立TCP连接 */
	snprintf(cmd, sizeof(cmd), "+IPSTART=\"TCP\",\"%s\",%u", CENTER_ADDR, CENTER_PORT);
	gprs_send_at(cmd);
	FOREVER
	{
		if ((ret = gprs_recv_ret_line(str, sizeof(str), SYSTEM_TICKS_PER_SEC * 4)) < 0)
		{
			if (-2 == ret)
			{
				GPRS_DEBUG(DEBUG_ERROR, "gprs recv time out\n");
				return -2;
			}
			GPRS_DEBUG(DEBUG_ERROR, "gprs_recv_ret_line error, ret=%d\n", ret);
			continue;
		}

		if (strcmp(str, "OK") == 0)
		{
			gprs_recv_ret_line(str, sizeof(str), SYSTEM_TICKS_PER_SEC * 4);
			if (strcmp(str, "CONNECT OK") == 0 || strcmp(str, "CONNECT") == 0)
			{
				gprs_is_connected = 1;
				break;
			}
			else
			{
				GPRS_DEBUG(DEBUG_ERROR, "%s\n", str);
				gprs_recv_ret_line(str, sizeof(str), SYSTEM_TICKS_PER_SEC * 4);
				return -1;
			}
		}
		else if (strcmp(str, "ERROR") == 0)
		{
			gprs_recv_ret_line(str, sizeof(str), SYSTEM_TICKS_PER_SEC * 4);
			GPRS_DEBUG(DEBUG_ERROR, "%s\n", str);
			return -1;
		}
		else if (strstr(str, "+CME ERROR:") != NULL)
		{
			GPRS_DEBUG(DEBUG_ERROR, "%s\n", str);
			return -1;
		}
		else
		{
			gprs_process_recv_cmd(str);
		}
	}
	
	return 0;
}

INT16 gprs_close(void)
{
	INT8 *p_cmd = NULL;
	INT8 str[24] = {0};
	INT16 ret = 0;
	
	if (0 == gprs_is_connected)
	{
		return 0;
	}

	/* 退出透传模式 */
	gprs_serial_send_onebyte((UINT8)'+');
	gprs_serial_send_onebyte((UINT8)'+');
	gprs_serial_send_onebyte((UINT8)'+');
	atomTimerDelay(SYSTEM_TICKS_PER_SEC * 2);

	p_cmd = "+IPCLOSE";
	gprs_send_at(p_cmd);
	while (1 == gprs_is_connected)
	{
		if ((ret = gprs_recv_ret_line(str, sizeof(str), SYSTEM_TICKS_PER_SEC * 4)) < 0)
		{
			if (-2 == ret)
			{
				GPRS_DEBUG(DEBUG_ERROR, "gprs recv time out\n");
				return -2;
			}
			GPRS_DEBUG(DEBUG_ERROR, "gprs_recv_ret_line error\n");
			continue;
		}

		if (strcmp(str, "+IPCLOSE:0") == 0)
		{
			gprs_is_connected = 0;
			gprs_recv_ret_line(str, sizeof(str), SYSTEM_TICKS_PER_SEC * 4);
			break;
		}
		else
		{
			gprs_process_recv_cmd(str);
		}
	}

	return 0;
}

void gprs_pwd_en(INT16 value)
{
	if (0 == value)
	{
		GPIO_ResetBits(GPIOB, GPIO_Pin_0);
	}
	else
	{
		GPIO_SetBits(GPIOB, GPIO_Pin_0);
	}
}

void gprs_pwrkey(INT16 value)
{
	if (0 == value)
	{
		GPIO_ResetBits(GPIOB, GPIO_Pin_1);
	}
	else
	{
		GPIO_SetBits(GPIOB, GPIO_Pin_1);
	}
}

static INT16 gprs_net_is_registered(void)
{
	INT8 str[16] = {0};
	INT8 *p_cmd = NULL;
	INT16 is_registered = 0;
	INT16 ret = 0;
	
	p_cmd = "+CREG?";
	gprs_send_at(p_cmd);
	FOREVER
	{
		if ((ret = gprs_recv_ret_line(str, sizeof(str), SYSTEM_TICKS_PER_SEC * 4)) < 0)
		{
			GPRS_DEBUG(DEBUG_ERROR, "gprs_recv_ret_line error\n");
			if (-2 == ret)
			{
				GPRS_DEBUG(DEBUG_ERROR, "gprs recv time out\n");
				return -1;
			}
			continue;
		}

		if (strstr(str, "+CREG:") != NULL)
		{
			if (strcmp(str, "+CREG: 0,1") == 0)
			{
				is_registered = 1;
			}
			gprs_recv_ret_line(str, sizeof(str), SYSTEM_TICKS_PER_SEC * 4);
			break;
		}
		else
		{
			gprs_process_recv_cmd(str);
		}
	}

	return is_registered;
}

static INT16 gprs_cgatt(void)
{
	INT8 str[16] = {0};
	INT8 *p_cmd = NULL;
	INT16 ret = 0;
	
	p_cmd = "+CGATT?";
	gprs_send_at(p_cmd);
	FOREVER
	{
		if ((ret = gprs_recv_ret_line(str, sizeof(str), SYSTEM_TICKS_PER_SEC * 4)) < 0)
		{
			GPRS_DEBUG(DEBUG_ERROR, "gprs_recv_ret_line error\n");
			if (-2 == ret)
			{
				GPRS_DEBUG(DEBUG_ERROR, "gprs recv time out\n");
				return -1;
			}
			continue;
		}

		if (strstr(str, "+CGATT:") != NULL)
		{
			if (strcmp(str, "+CGATT: 1") == 0)
			{
				ret = 1;
			}
			gprs_recv_ret_line(str, sizeof(str), SYSTEM_TICKS_PER_SEC * 4);
			break;
		}
		else
		{
			gprs_process_recv_cmd(str);
		}
	}

	return ret;
}

static INT16 gprs_modem_init(void)
{
	INT8 str[64] = {0};
	INT8 *p_cmd = NULL;
	INT16 ret = 0;
	INT16 count = 8; // 初始化时尝试发送AT指令的次数

	while (count > 0)
	{
		clear_gprs_buffer();

		gprs_send_at("");

		if (gprs_recv_ret_line(str, sizeof(str), SYSTEM_TICKS_PER_SEC * 2) < 0)
		{
			GPRS_DEBUG(DEBUG_ERROR, "gprs_recv_ret_line error\n");
		}
		if (strcmp(str, "OK") == 0)
		{
			break;
		}
		else
		{
			GPRS_DEBUG(DEBUG_ERROR, "recv error str=%s\n", str);
		}

		count--;
	}

	if (0 == count)
	{
		GPRS_DEBUG(DEBUG_ERROR, "gprs modem does not answer\n");
		return -1;
	}

	/* 获取IMEI作为设备的唯一ID */
	p_cmd = "+CGSN";
	gprs_send_at(p_cmd);
	count = 0;
	FOREVER
	{
		if (++count > 10)
		{
			GPRS_DEBUG(DEBUG_ERROR, "try time exceed\n");
			return -1;
		}
		if ((ret = gprs_recv_ret_line(str, sizeof(str), SYSTEM_TICKS_PER_SEC * 4)) < 0)
		{
			GPRS_DEBUG(DEBUG_ERROR, "gprs_recv_ret_line error\n");
			if (-2 == ret)
			{
				GPRS_DEBUG(DEBUG_ERROR, "gprs recv time out\n");
				return -1;
			}
			continue;
		}

		if (strstr(str, "+CGSN:") != NULL)
		{
			memset(g_unique_id, 0, sizeof(g_unique_id));
			if (sscanf(str, "+CGSN: 0,%s", g_unique_id) != 1)
			{
				GPRS_DEBUG(DEBUG_ERROR, "can not get gprs IMEI\n");
				return -1;
			}
			printf("unique id:%s\n", g_unique_id);
			gprs_recv_ret_line(str, sizeof(str), SYSTEM_TICKS_PER_SEC * 4);
			break;
		}
		else
		{
			gprs_process_recv_cmd(str);
		}
	}

	/* 检查是否已经注册到网络 */
	count = 0;
	FOREVER
	{
		if (++count > 10)
		{
			GPRS_DEBUG(DEBUG_ERROR, "try time exceed\n");
			return -1;
		}
		ret = gprs_net_is_registered();
		if (ret < 0)
		{
			GPRS_DEBUG(DEBUG_ERROR, "gprs register failed\n");
			return -1;
		}
		if (1 == ret)
		{
			atomTimerDelay(SYSTEM_TICKS_PER_SEC);
			break;
		}

		GPRS_DEBUG(DEBUG_WARN, "gprs register failed\n");
		atomTimerDelay(SYSTEM_TICKS_PER_SEC * 2);
	}

	/* 查询GPRS附着状态 */
	count = 0;
	FOREVER
	{
		if (++count > 10)
		{
			GPRS_DEBUG(DEBUG_ERROR, "try time exceed\n");
			return -1;
		}
		ret = gprs_cgatt();
		if (ret < 0)
		{
			GPRS_DEBUG(DEBUG_ERROR, "gprs_cgatt failed\n");
			return -1;
		}
		if (1 == ret)
		{
			atomTimerDelay(SYSTEM_TICKS_PER_SEC);
			break;
		}
		GPRS_DEBUG(DEBUG_WARN, "gprs_cgatt failed\n");
		atomTimerDelay(SYSTEM_TICKS_PER_SEC * 2);
	}

	/* 设置单路连接 */
	p_cmd = "+CMMUX=0";
	gprs_send_at(p_cmd);
	count = 0;
	FOREVER
	{
		if (++count > 10)
		{
			GPRS_DEBUG(DEBUG_ERROR, "try time exceed\n");
			return -1;
		}
		if ((ret = gprs_recv_ret_line(str, sizeof(str), SYSTEM_TICKS_PER_SEC * 4)) < 0)
		{
			GPRS_DEBUG(DEBUG_ERROR, "gprs_recv_ret_line error\n");
			if (-2 == ret)
			{
				GPRS_DEBUG(DEBUG_ERROR, "gprs recv time out\n");
				return -1;
			}
			continue;
		}
		if (strcmp(str, "OK") == 0)
		{
			break;
		}
		else if (strcmp(str, "ERROR") == 0)
		{
			GPRS_DEBUG(DEBUG_ERROR, "cmd %s return ERROR\n", p_cmd);
			break;
		}
		else
		{
			gprs_process_recv_cmd(str);
		}
	}

	/* 使用NTP命令前先取消透传模式 */
	p_cmd = "+CMMODE=0";
	gprs_send_at(p_cmd);
	count = 0;
	FOREVER
	{
		if (++count > 10)
		{
			GPRS_DEBUG(DEBUG_ERROR, "try time exceed\n");
			return -1;
		}
		if ((ret = gprs_recv_ret_line(str, sizeof(str), SYSTEM_TICKS_PER_SEC * 4)) < 0)
		{
			if (-2 == ret)
			{
				GPRS_DEBUG(DEBUG_ERROR, "gprs recv time out\n");
				return -1;
			}
			GPRS_DEBUG(DEBUG_ERROR, "gprs_recv_ret_line error\n");
			continue;
		}
		if (strcmp(str, "OK") == 0)
		{
			break;
		}
		else
		{
			gprs_process_recv_cmd(str);
		}
	}
	atomTimerDelay(SYSTEM_TICKS_PER_SEC);

	/* NTP校时相关设置 */
	p_cmd = "+CGACT=1,1"; // 使用NTP校时必须先使用这条命令进行设置
	gprs_send_at(p_cmd);
	count = 0;
	FOREVER
	{
		if (++count > 10)
		{
			GPRS_DEBUG(DEBUG_ERROR, "try time exceed\n");
			return -1;
		}
		if ((ret = gprs_recv_ret_line(str, sizeof(str), SYSTEM_TICKS_PER_SEC * 4)) < 0)
		{
			GPRS_DEBUG(DEBUG_ERROR, "gprs_recv_ret_line error\n");
			if (-2 == ret)
			{
				GPRS_DEBUG(DEBUG_ERROR, "gprs recv time out\n");
				return -1;
			}
			continue;
		}
		if (strcmp(str, "OK") == 0)
		{
			break;
		}
		else
		{
			gprs_process_recv_cmd(str);
		}
	}
	p_cmd = "+CMNTP=\"112.124.41.233\""; // 写死NTP服务器为自己的阿里云服务器
	gprs_send_at(p_cmd);
	count = 0;
	FOREVER
	{
		if (++count > 10)
		{
			GPRS_DEBUG(DEBUG_ERROR, "try time exceed\n");
			return -1;
		}
		if ((ret = gprs_recv_ret_line(str, sizeof(str), SYSTEM_TICKS_PER_SEC * 4)) < 0)
		{
			GPRS_DEBUG(DEBUG_ERROR, "gprs_recv_ret_line error\n");
			if (-2 == ret)
			{
				GPRS_DEBUG(DEBUG_ERROR, "gprs recv time out\n");
				return -1;
			}
			continue;
		}
		if (strcmp(str, "OK") == 0)
		{
			break;
		}
		else
		{
			gprs_process_recv_cmd(str);
		}
	}

	return 0;
}

void gprs_shutdown(void)
{
	if (gprs_is_on)
	{
		gprs_pwrkey(0);
		gprs_pwd_en(0);
		gprs_is_on = 0;
	}
	return;
}

INT16 gprs_init(void)
{
	if (!gprs_is_on)
	{
		gprs_pwd_en(1);
		gprs_pwrkey(1);
		gprs_is_on = 1;
	}

	atomTimerDelay(SYSTEM_TICKS_PER_SEC * 10);
	
	return gprs_modem_init();
}

INT16 gprs_write(const void *p_data, INT16 len)
{
	UINT8 *ptr = (UINT8 *)p_data;
	INT16 i = 0;
	
	if (NULL == p_data)
	{
		GPRS_DEBUG(DEBUG_ERROR, "param error\n");
		return -1;
	}

	if (!gprs_is_connected)
	{
		GPRS_DEBUG(DEBUG_ERROR, "GPRS MODEM has not connected\n");
		return -1;
	}

	for (i = 0; i < len; i++)
	{
		gprs_serial_send_onebyte(ptr[i]);
	}

	return len;
}

INT16 gprs_readn(void *p_data, INT16 len, INT32 timeout)
{
	INT16 read_byte = 0;
	UINT8 *ptr = (UINT8 *)p_data;
	INT16 ret = 0;
	
	if (NULL == p_data)
	{
		GPRS_DEBUG(DEBUG_ERROR, "param error\n");
		return -1;
	}

	if (!gprs_is_connected)
	{
		GPRS_DEBUG(DEBUG_ERROR, "GPRS MODEM has not connected\n");
		return -1;
	}

	while (len > 0)
	{
		ret = gprs_serial_read_onebyte(ptr, timeout);
		if (ret != 0)
		{
			GPRS_DEBUG(DEBUG_WARN, "gprs_serial_read_onebyte failed, ret=%d\n", ret);
			break;
		}

		ptr++;
		read_byte++;
		len--;
	}

	if (-2 == ret || len == 0)
	{
		return read_byte;
	}
	else
	{
		return ret;
	}
}

INT16 gprs_get_ntp_time(INT8 *p_time, INT16 len)
{
	INT8 *p_cmd = NULL;
	INT8 str[64] = {0};
	INT16 ret = 0;
	
	if (NULL == p_time)
	{
		GPRS_DEBUG(DEBUG_ERROR, "param error\n");
		return -1;
	}

	if (gprs_is_connected)
	{
		GPRS_DEBUG(DEBUG_ERROR, "gprs tcp must be closed before get ntp time\n");
		return -1;
	}

	/* 校时前必须先取消透传模式 */
	p_cmd = "+CMMODE=0";
	gprs_send_at(p_cmd);
	FOREVER
	{
		if ((ret = gprs_recv_ret_line(str, sizeof(str), SYSTEM_TICKS_PER_SEC * 4)) < 0)
		{
			if (-2 == ret)
			{
				GPRS_DEBUG(DEBUG_ERROR, "gprs recv time out\n");
				return -2;
			}
			GPRS_DEBUG(DEBUG_ERROR, "gprs_recv_ret_line error\n");
			continue;
		}
		if (strcmp(str, "OK") == 0)
		{
			break;
		}
		else
		{
			gprs_process_recv_cmd(str);
		}
	}
	atomTimerDelay(SYSTEM_TICKS_PER_SEC); // 增加延时，如果设置非透传模式后立刻校时，有很高的概率GPRS模块卡死不返回

	/* 获取ntp时间 */
	p_cmd = "+CMNTP";
	gprs_send_at(p_cmd);
	FOREVER
	{
		if ((ret = gprs_recv_ret_line(str, sizeof(str), SYSTEM_TICKS_PER_SEC * 20)) < 0)
		{
			if (-2 == ret)
			{
				GPRS_DEBUG(DEBUG_ERROR, "gprs recv time out\n");
				return -2;
			}
			GPRS_DEBUG(DEBUG_ERROR, "gprs_recv_ret_line error\n");
			continue;
		}

		if (strstr(str, "+CMNTP:") != NULL)
		{
			if (strstr(str, "+CMNTP: 0") != NULL)
			{
				GPRS_DEBUG(DEBUG_ERROR, "ntp time return 0\n");
				return -1;
			}
			snprintf(p_time, len, "%s", str + strlen("+CMNTP:"));
			break;
		}
		else
		{
			gprs_process_recv_cmd(str);
		}
	}

	return 0;
}

