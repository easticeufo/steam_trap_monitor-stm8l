      
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

#ifndef _GPRS_H_
#define _GPRS_H_

#include "base_fun.h"

INT16 gprs_init(void);
void gprs_shutdown(void);

INT16 gprs_connect(void);
INT16 gprs_close(void);

INT16 gprs_write(const void *p_data, INT16 len);
INT16 gprs_readn(void *p_data, INT16 len, INT32 timeout);
INT16 gprs_get_ntp_time(INT8 *p_time, INT16 len);

#endif

