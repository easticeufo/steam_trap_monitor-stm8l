      
/**@file 
 * @note tiansixinxi. All Right Reserved.
 * @brief  
 * 
 * @author   madongfang
 * @date     2016-5-26
 * @version  V1.0.0
 * 
 * @note ///Description here 
 * @note History:        
 * @note     <author>   <time>    <version >   <desc>
 * @note  
 * @warning  
 */

#ifndef _BASE_FUN_H_
#define _BASE_FUN_H_

#include "common.h"

/* 调试打印 */
#define DEBUG_NONE 0 // 不输出调试打印信息
#define DEBUG_ERROR 1 // 输出错误信息
#define DEBUG_WARN 2 // 输出警告信息
#define DEBUG_NOTICE 3 // 输出一般低频调试信息
#define DEBUG_INFO 4 // 输出一般高频调试信息

/* 根据不同等级输出调试打印信息 */
#define DEBUG_PRINT(level, fmt, args...) \
	do\
	{\
		if (level <= get_debug_level())\
		{\
			if (DEBUG_ERROR == level)\
			{\
				printf("[%s][%s][%d]"fmt, "ERROR", __FILE__, __LINE__, ##args);\
			}\
			else if (DEBUG_WARN == level)\
			{\
				printf("[%s][%s][%d]"fmt, "WARNING", __FILE__, __LINE__, ##args);\
			}\
			else if (DEBUG_NOTICE == level)\
			{\
				printf("[%s][%s][%d]"fmt, "NOTICE", __FILE__, __LINE__, ##args);\
			}\
			else if (DEBUG_INFO == level)\
			{\
				printf("[%s][%s][%d]"fmt, "INFO", __FILE__, __LINE__, ##args);\
			}\
		}\
	}while(0)

/* 安全释放动态分配的内存 */
#define SAFE_FREE(ptr) \
	do\
	{\
		if (NULL != ptr)\
		{\
			free(ptr);\
			ptr = NULL;\
		}\
	}while(0)

/* 安全关闭描述符 */
#define SAFE_CLOSE(fd) \
	do\
	{\
		if (-1 != fd)\
		{\
			close(fd);\
			fd = -1;\
		}\
	}while(0)

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

extern INT32 get_debug_level(void);
extern void set_debug_level(INT32 level);
extern UINT32 get_build_date(INT8 *p_date_buff, INT32 buff_len);

#endif

