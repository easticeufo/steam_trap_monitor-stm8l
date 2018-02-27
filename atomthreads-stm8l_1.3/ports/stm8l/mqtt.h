      
/**@file 
 * @note
 * @brief  
 * 
 * @author   madongfang
 * @date     2015-12-5
 * @version  V1.0.0
 * 
 * @note ///Description here 
 * @note History:        
 * @note     <author>   <time>    <version >   <desc>
 * @note  
 * @warning  
 */

#ifndef _MQTT_H_
#define _MQTT_H_

#include "base_fun.h"

extern INT16 mqtt_connect(UINT16 keep_alive_interval);
extern void mqtt_disconnect(void);
extern INT16 mqtt_subscrib(const INT8 *p_topic, INT8 *p_msg_buff, INT16 msg_buff_len);
extern INT16 mqtt_publish(INT8 *p_topic, INT8 *p_message);

#endif

