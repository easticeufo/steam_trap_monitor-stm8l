      
/**@file 
 * @note 
 * @brief  
 * 
 * @author   madongfang
 * @date     2016-10-16
 * @version  V1.0.0
 * 
 * @note ///Description here 
 * @note History:        
 * @note     <author>   <time>    <version >   <desc>
 * @note  
 * @warning  
 */

#include <atom.h>
#include "base_fun.h"
#include "gprs.h"
#include "MQTTPacket.h"

#define MQTT_USERNAME "admin"
#define MQTT_PASSWORD "nonadmin"

extern INT8 g_unique_id[24];

static UINT8 mqtt_buff[200] = {0}; // 为了节省内存，采用全局变量提供公用的mqtt数据组装缓存，多线程同时访问需要互斥

static INT16 transport_getdata(UINT8 *buff, INT16 len)
{
	return gprs_readn(buff, len, SYSTEM_TICKS_PER_SEC * 4);
}

INT16 mqtt_connect(UINT16 keep_alive_interval)
{
	MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
	INT16 len = 0;
	UINT8 session_present = 0;
	UINT8 connack_rc = 0xff;
	INT16 ret = 0;

	if (gprs_connect() != 0)
	{
		DEBUG_PRINT(DEBUG_ERROR, "gprs connect failed!\n");
		return -1;
	}
	
	/* 发送mqtt connect请求 */
	data.clientID.cstring = g_unique_id;
	data.keepAliveInterval = keep_alive_interval; //服务器保持连接时间，超过该时间后，服务器会主动断开连接，单位为秒
	data.cleansession = 1;
	data.MQTTVersion = 3;
	data.username.cstring = MQTT_USERNAME;
	data.password.cstring = MQTT_PASSWORD;
	len = MQTTSerialize_connect(mqtt_buff, sizeof(mqtt_buff), &data);
	if (len <= 0)
	{
		DEBUG_PRINT(DEBUG_ERROR, "MQTTSerialize_connect error ret=%d\n", len);
		gprs_close();
		return -1;
	}
	if (gprs_write(mqtt_buff, len) < 0)
	{
		DEBUG_PRINT(DEBUG_ERROR, "gprs write failed!\n");
		gprs_close();
		return -1;
	}

	/* 接收并处理mqtt connect响应 */
	if ((ret = MQTTPacket_read(mqtt_buff, sizeof(mqtt_buff), transport_getdata)) != CONNACK)
	{
		DEBUG_PRINT(DEBUG_ERROR, "MQTTPacket_read CONNACK failed! ret=%d\n", ret);
		gprs_close();
		return -1;
	}
	if (MQTTDeserialize_connack(&session_present, &connack_rc, mqtt_buff, sizeof(mqtt_buff)) != 1 
		|| connack_rc != 0)
	{
		DEBUG_PRINT(DEBUG_ERROR, "CONNACK Return Code = 0x%02x\n", connack_rc);
		gprs_close();
		return -1;
	}

	return 0;
}

void mqtt_disconnect(void)
{
	INT16 len = 0;
	
	len = MQTTSerialize_disconnect(mqtt_buff, sizeof(mqtt_buff));
	if (len > 0)
	{
		if (gprs_write(mqtt_buff, len) < 0)
		{
			DEBUG_PRINT(DEBUG_ERROR, "gprs write failed!\n");
		}
	}
	else
	{
		DEBUG_PRINT(DEBUG_ERROR, "MQTTSerialize_disconnect error ret=%d\n", len);
	}	

	if (gprs_close() !=0)
	{
		DEBUG_PRINT(DEBUG_ERROR, "gprs_close failed!\n");
	}

	return;
}

INT16 mqtt_subscrib(INT8 *p_topic, INT8 *p_msg_buff, INT16 msg_buff_len)
{
	MQTTString subscribe_topic = MQTTString_initializer;
	INT16 len = 0;
	INT16 qos = 0;
	UINT8 dup = 0;
	UINT8 retained = 0;
	UINT16 msgid = 0;
	MQTTString received_topic = MQTTString_initializer;
	UINT8 *p_payload = NULL;
	INT16 payload_len = 0;
	
	if (NULL == p_topic || NULL == p_msg_buff || msg_buff_len < 0)
	{
		DEBUG_PRINT(DEBUG_ERROR, "param error\n");
		return -1;
	}

	/* 发送subscrib请求 */
	subscribe_topic.cstring = p_topic;
	len = MQTTSerialize_subscribe(mqtt_buff, sizeof(mqtt_buff), 0, 1, 1, &subscribe_topic, &qos);
	if (len <= 0)
	{
		DEBUG_PRINT(DEBUG_ERROR, "MQTTSerialize_subscribe error ret=%d\n", len);
		return -1;
	}
	if (gprs_write(mqtt_buff, len) < 0)
	{
		DEBUG_PRINT(DEBUG_ERROR, "gprs write failed!\n");
		return -1;
	}

	/* 接收subscrib响应 */
	if (MQTTPacket_read(mqtt_buff, sizeof(mqtt_buff), transport_getdata) != SUBACK)
	{
		DEBUG_PRINT(DEBUG_ERROR, "MQTTPacket_read SUBACK failed!\n");
		return -1;
	}

	/* 接收并处理Retained的publish数据 */
	if (MQTTPacket_read(mqtt_buff, sizeof(mqtt_buff), transport_getdata) != PUBLISH)
	{
		DEBUG_PRINT(DEBUG_ERROR, "MQTTPacket_read PUBLISH failed!\n");
		return -1;
	}
	if (MQTTDeserialize_publish(&dup, &qos, &retained, &msgid, &received_topic
		, &p_payload, &payload_len, mqtt_buff, sizeof(mqtt_buff)) != 1)
	{
		DEBUG_PRINT(DEBUG_ERROR, "MQTTDeserialize_publish error\n");
		return -1;
	}

	memset(p_msg_buff, 0, msg_buff_len);
	if (payload_len >= msg_buff_len)
	{
		DEBUG_PRINT(DEBUG_ERROR, "payload exceed message buffer, payload_len=%d\n", payload_len);
		return -1;
	}
	memcpy(p_msg_buff, p_payload, payload_len);

	return 0;
}

INT16 mqtt_publish(INT8 *p_topic, INT8 *p_message)
{
	MQTTString topic_string = MQTTString_initializer;
	INT16 len = 0;

	topic_string.cstring = p_topic;
	len = MQTTSerialize_publish(mqtt_buff, sizeof(mqtt_buff), 0, 0, 0, 0, topic_string, (UINT8 *)p_message, strlen(p_message));
	if (len <= 0)
	{
		DEBUG_PRINT(DEBUG_ERROR, "MQTTSerialize_publish error ret=%d\n", len);
		return -1;
	}
	
	if (gprs_write(mqtt_buff, len) == ERROR)
	{
		DEBUG_PRINT(DEBUG_ERROR, "mqtt_publish gprs_write error\n");
		return -1;
	}
	
	return 0;
}

