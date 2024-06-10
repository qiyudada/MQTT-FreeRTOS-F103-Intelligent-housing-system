#include "ATcommand.h"

static int ATstatus = 0;
static ring_buffer g_packet_buffer;
static platform_mutex_t AT_ret_mutex;
static platform_mutex_t AT_packet_mutex;
//static platform_mutex_t AT_Send_mutex;

static char AT_resp[AT_RESP_LEN];
//static char AT_packet[AT_RESP_LEN];
//static int AT_packet_len;

/*function: SetATstatus
*description: set AT status
*parameters: status: AT status
            0: AT status is ok
            -1: AT status is error
            -2: AT status is timeout
*/
int SetATstatus(int status)
{
    ATstatus = status;
    return 0;
}
/*function:GetATstatus
 *description: get AT status
 *parameters: none
 *return: AT status
 */
int GetATstatus(void)
{
    return ATstatus;
}

int ATInit(void)
{
    platform_mutex_init(&AT_ret_mutex);
    platform_mutex_lock(&AT_ret_mutex); // mutex = 0

    platform_mutex_init(&AT_packet_mutex);
    platform_mutex_lock(&AT_packet_mutex); // mutex = 0

//    platform_mutex_init(&AT_Send_mutex);
//    platform_mutex_lock(&AT_Send_mutex);

    ring_buffer_init(&g_packet_buffer);
    return 0;
}

int AT_SendCmd(char *cmd, char *resp, int resp_len, int timeout)
{
    int ret;
    int err;

   // platform_mutex_lock(&AT_Send_mutex);

    Hal_Uart_Send(cmd, strlen(cmd));
    Hal_Uart_Send("\r\n", 2);

   // platform_mutex_unlock(&AT_Send_mutex);

    ret = platform_mutex_lock_timeout(&AT_ret_mutex, timeout);
    if (ret)
    {
        err = GetATstatus();
        if (!err && resp)
        {
            memcpy(resp, AT_resp, resp_len > AT_RESP_LEN ? AT_RESP_LEN : resp_len);
        }
        return err;
    }
    else
    {
        return AT_TIMEOUT;
    }
}

int GetSpecialATString(char *buf)
{
    if (strstr(buf, "+IPD,"))
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

static void ProcessSpecialATString(char *buf)
{
    int i = 0;
    int len = 0;
    /* +IPD,78:xxxxxxxxxx */

    /* 解析出长度 */

    while (1)
    {
        HAL_AT_Receive(&buf[i], (int)portMAX_DELAY);
        if (buf[i] == ':')
        {

            break;
        }
        else
        {
            len = len * 10 + (buf[i] - '0');
        }
        i++;
    }

    /* 读取真正的数据 */
		i = 0;
		while (i < len)
		{
			HAL_AT_Receive(&buf[i], (int)portMAX_DELAY);
			if (i < AT_RESP_LEN)
			{
				/* 把数据放入环形buffer */
				ring_buffer_write(buf[i], &g_packet_buffer);
				
				/* wake up */
				/* 解锁 */
				platform_mutex_unlock(&AT_packet_mutex);		
			}
			i++;
		}
}

int ATSendData(unsigned char *buf, int len, int timeout)
{
    int ret;
    int err;

    /* 发生AT命令 */
    Hal_Uart_Send((char *)buf, len);

    /* 等待结果
     * 1 : 成功得到mutex
     * 0 : 超时返回
     */
    ret = platform_mutex_lock_timeout(&AT_ret_mutex, timeout);
    if (ret)
    {
        /* 判断返回值 */
        /* 存储resp */
        err = GetATstatus();
        return err;
    }
    else
    {
        return AT_TIMEOUT;
    }
}

int ATReadData(unsigned char *c, int timeout)
{
    int ret;

    do
    {
        if (0 == ring_buffer_read((unsigned char *)c, &g_packet_buffer))
        {
            return AT_OK;
        }
        else
        {
            ret = platform_mutex_lock_timeout(&AT_packet_mutex, timeout);
            if (0 == ret)
            {
                return AT_TIMEOUT;
            }
        }
    } while ((ret == 1));

    return 0;
}
/*function:AT_RecvParse
*description: AT receive parse
*parameters: none
*return: none
*/
void AT_RecvParse(void *arg)
{
    char Temp_buf[AT_RESP_LEN];
    int i = 0;
		
		
    while (1)
    {

        HAL_AT_Receive(&Temp_buf[i], (int)portMAX_DELAY);  
				
				Temp_buf[i + 1] = '\0';

        if (i && (Temp_buf[i - 1] == '\r') && (Temp_buf[i] == '\n'))
        {
						
            if (strstr(Temp_buf, "OK\r\n"))
            {
                memcpy(AT_resp, Temp_buf, i);
                SetATstatus(AT_OK);
                platform_mutex_unlock(&AT_ret_mutex);
                i = 0;
            }
            else if (strstr(Temp_buf, "ERROR\r\n"))
            {
				memcpy(AT_resp,Temp_buf, i);
                SetATstatus(AT_ERROR);// AT_ERROR = -1
                platform_mutex_unlock(&AT_ret_mutex);
                i = 0;
            }
						i=0;
        }
        else if (GetSpecialATString(Temp_buf))
        {
            ProcessSpecialATString(Temp_buf);
            i = 0;
        }
        else
        {
            i++;
        }
        if (i >= AT_RESP_LEN)
        {
            i = 0;
        }
    }
}

static void topic1_handler(void *client, message_data_t *msg)
{
    (void)client;
    MQTT_LOG_I("-----------------------------------------------------------------------------------");
    MQTT_LOG_I("%s:%d %s()...\r\ntopic: %s\r\nmessage:%s", __FILE__, __LINE__, __FUNCTION__, msg->topic_name, (char *)msg->message->payload);
    MQTT_LOG_I("-----------------------------------------------------------------------------------");
}

void MQTT_Client_Task(void *Param)
{
    int err;

    char buf[50];
    int cnt = 0;

    mqtt_client_t *client = NULL;
    mqtt_message_t msg;

    memset(&msg, 0, sizeof(msg));

    mqtt_log_init();

    client = mqtt_lease();

    mqtt_set_port(client, "1883");

//     mqtt_set_host(client, "192.168.11.141");
    mqtt_set_host(client, "47.114.187.247"); // iot.100ask.net
    mqtt_set_client_id(client, random_string(10));
    mqtt_set_user_name(client, random_string(10));
    mqtt_set_password(client, random_string(10));
    mqtt_set_clean_session(client, 1);

    if (0 != mqtt_connect(client))
    {
        printf("mqtt_connect err\r\n");
        vTaskDelete(NULL);
    }

    err = mqtt_subscribe(client, "topic1", QOS0, topic1_handler);
    if (err)
    {
        printf("mqtt_subscribe topic1 err\r\n");
    }

    err = mqtt_subscribe(client, "topic2", QOS1, NULL);
    if (err)
    {
        printf("mqtt_subscribe topic2 err\r\n");
    }

    err = mqtt_subscribe(client, "topic3", QOS2, NULL);
    if (err)
    {
        printf("mqtt_subscribe topic3 err\r\n");
    }

    msg.payload =(void *)buf;
		msg.qos = QOS0;
		
		msg.payloadlen = strlen(buf);
		mqtt_publish(client, "mcu_test", &msg);

		err = mqtt_subscribe(client, "mcu_test", QOS0, NULL);	  
		printf("subscribe err = %d\r\n", err);

    while (1)
    {
        sprintf(buf, "100ask, %d", cnt++);
				msg.payload =(void *)buf;
				msg.qos = QOS0;
        msg.payloadlen = strlen(msg.payload);
        mqtt_publish(client, "mcu_test", &msg);
        vTaskDelay(5000);
    }
}