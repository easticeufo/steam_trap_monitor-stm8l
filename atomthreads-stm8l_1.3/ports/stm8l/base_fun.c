      
/**@file 
 * @note tiansixinxi. All Right Reserved.
 * @brief  
 * 
 * @author  madongfang
 * @date     2016-5-26
 * @version  V1.0.0
 * 
 * @note ///Description here 
 * @note History:        
 * @note     <author>   <time>    <version >   <desc>
 * @note  
 * @warning  
 */

#include "base_fun.h"

static INT32 debug_level = DEBUG_ERROR; // 调试打印信息输出等级

/**		  
 * @brief 获取调试打印信息输出等级
 * @param 无
 * @return 调试打印信息输出等级
 */
INT32 get_debug_level(void)
{
	return debug_level;
}

/**		  
 * @brief		设置调试打印信息输出等级 
 * @param[in] level 调试打印信息输出等级
 * @return 无
 */
void set_debug_level(INT32 level)
{
	debug_level = level;
}

/**
 * @brief 获取程序编译日期，返回一个整数表示的日期,若p_date_buff不是NULL，在p_date_buff中返回编译日期的字符串
 * @param[out] p_date_buff 将日期按照格式"build yyyymmdd"存放
 * @param[in] buff_len 存放编译日期字符串的缓冲区长度
 * @return 返回一个整数表示日期,16~31位表示年份,8~15位表示月份,0~7位表示日期
 */
UINT32 get_build_date(INT8 *p_date_buff, INT32 buff_len)
{
	INT32 year = 0;
	INT32 month = 0;
	INT32 day = 0;
	INT8 month_name[4] = {0};
	const INT8 *all_mon_names[] 
		= {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

	sscanf(__DATE__, "%s%ld%ld", month_name, &day, &year);

	for (month = 0; month < 12; month++)
	{
		if (strcmp(month_name, all_mon_names[month]) == 0)
		{
			break;
		}
	}
	month++;

	if (p_date_buff != NULL)
	{
		snprintf(p_date_buff, buff_len, "build %ld%02ld%02ld", year, month, day);
	}

	return (UINT32)((year << 16) | (month << 8) | day);
}

