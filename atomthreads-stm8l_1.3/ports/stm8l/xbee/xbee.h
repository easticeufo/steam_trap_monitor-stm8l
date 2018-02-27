      
/**@file 
 * @note
 * @brief  
 * 
 * @author   madongfang
 * @date     2016-10-23
 * @version  V1.0.0
 * 
 * @note ///Description here 
 * @note History:        
 * @note     <author>   <time>    <version >   <desc>
 * @note  
 * @warning  
 */

#ifndef _XBEE_H_
#define _XBEE_H_

INT16 xbee_init(void);
INT16 xbee_recv_ret_str(INT8 *p_buff, INT16 len, INT32 timeout);
INT16 xbee_write(const void *p_data, INT16 len);
INT16 xbee_get_time(INT8 *p_time, INT16 len);
void xbee_sleep(void);
void xbee_wakeup(void);

#endif

