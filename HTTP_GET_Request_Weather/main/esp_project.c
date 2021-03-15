/*
	HTTP Get Weather请求测试
	初始化站模式，连接WIFI，连接成功后请求HTTP打印返回内容
 */
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "cJSON.h"
#include "cJSON_Utils.h"

#define TAG "HTTP Get Weather Example"




//http组包宏，获取天气的http接口参数
#define WEB_SERVER          "www.weather.com.cn"              
#define WEB_PORT            "80"
#define WEB_URL             "/data/sk/101010100.html"
#define host 		        "www.weather.com.cn"
static const int CONNECTED_BIT = BIT0;


//http请求包
static const char *REQUEST = "GET "WEB_URL" HTTP/1.1\r\n"
    "Host: "WEB_SERVER"\r\n"
    "Connection: close\r\n"
    "\r\n";

//wifi链接成功事件
static EventGroupHandle_t wifi_event_group;

// HTTP 请求任务
void http_get_task(void *pvParameters);

// wifi事件处理
static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
	switch (event->event_id) {
		case SYSTEM_EVENT_STA_START:
			esp_wifi_connect();
			break;
		case SYSTEM_EVENT_STA_GOT_IP:
			xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
			xTaskCreate(http_get_task, "http_get_task", 4096, NULL, 3, NULL);

			break;
		case SYSTEM_EVENT_STA_DISCONNECTED:
			esp_wifi_connect();
			xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
			break;
		default:
			break;
	}
	return ESP_OK;
}
// wifi初始化
static void wifi_init(void)
{
	tcpip_adapter_init();
	wifi_event_group = xEventGroupCreate();
	ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	wifi_config_t wifi_config = {
		.sta = {
			.ssid = "HelloBug",
			.password = "12345678",
		},
	};
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
	ESP_LOGI(TAG, "start the WIFI SSID:[%s]", wifi_config.sta.ssid);
	ESP_ERROR_CHECK(esp_wifi_start());
	ESP_LOGI(TAG, "Waiting for wifi");
	// 等待wifi连上
	xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
}

// 解析json数据 只处理 解析 城市 天气 天气代码  温度  其他的自行扩展
void cjson_to_struct_info(char *text)
{
	cJSON *root,*psub;
	cJSON *Item;

	//截取有效json
	char *index=strchr(text,'{');
	strcpy(text,index);
	root = cJSON_Parse(text);
	if(root!=NULL){
		psub = cJSON_GetObjectItem(root, "weatherinfo");
		if(psub!=NULL){
			Item=cJSON_GetObjectItem(psub,"cityid");
			if(Item!=NULL){
				ESP_LOGI(TAG,"cityid:%s\n",Item->valuestring);
			}

			Item=cJSON_GetObjectItem(psub,"city");
			if(Item!=NULL){
				ESP_LOGI(TAG,"城市:%s\n",Item->valuestring);
			}

			Item=cJSON_GetObjectItem(psub,"WD");
			if(Item!=NULL){
				ESP_LOGI(TAG,"风向:%s\n",Item->valuestring);
			}

			Item=cJSON_GetObjectItem(psub,"temp");
			if(Item!=NULL){
				ESP_LOGI(TAG,"温度:%s\n",Item->valuestring);
			}

			Item=cJSON_GetObjectItem(psub,"SD");
			if(Item!=NULL){
				ESP_LOGI(TAG,"湿度:%s\n",Item->valuestring);
			}

			Item=cJSON_GetObjectItem(psub,"AP");
			if(Item!=NULL){
				ESP_LOGI(TAG,"气压:%s\n",Item->valuestring);
			}

			Item=cJSON_GetObjectItem(psub,"time");
			if(Item!=NULL){
				ESP_LOGI(TAG,"时间:%s\n",Item->valuestring);
			}
		}
	}
	cJSON_Delete(root);
}
// http任务
void http_get_task(void *pvParameters)
{
	const struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct addrinfo *res;
	struct in_addr *addr;
	int s, r;
	char recv_buf[1024];
	char mid_buf[1024];
	int index;
	while(1) {
		//DNS域名解析
		int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);
		if(err != 0 || res == NULL) {
			ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p\r\n", err, res);
			vTaskDelay(1000 / portTICK_PERIOD_MS);
			continue;
		}

		//打印获取的IP
		addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
		//ESP_LOGI(HTTP_TAG, "DNS lookup succeeded. IP=%s\r\n", inet_ntoa(*addr));

		//新建socket
		s = socket(res->ai_family, res->ai_socktype, 0);
		if(s < 0) {
			ESP_LOGE(TAG, "... Failed to allocate socket.\r\n");
			close(s);
			freeaddrinfo(res);
			vTaskDelay(1000 / portTICK_PERIOD_MS);
			continue;
		}
		//连接ip
		if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
			ESP_LOGE(TAG, "... socket connect failed errno=%d\r\n", errno);
			close(s);
			freeaddrinfo(res);
			vTaskDelay(4000 / portTICK_PERIOD_MS);
			continue;
		}
		freeaddrinfo(res);
		//发送http包
		if (write(s, REQUEST, strlen(REQUEST)) < 0) {
			ESP_LOGE(TAG, "... socket send failed\r\n");
			close(s);
			vTaskDelay(4000 / portTICK_PERIOD_MS);
			continue;
		}
		//清缓存
		memset(mid_buf,0,sizeof(mid_buf));
		//获取http应答包
		do {
			bzero(recv_buf, sizeof(recv_buf));
			r = read(s, recv_buf, sizeof(recv_buf)-1);
			strcat(mid_buf,recv_buf);
		} while(r > 0);
		ESP_LOGE(TAG, "Rev:%s\n",mid_buf);
		//json解析
		cjson_to_struct_info(mid_buf);
		//关闭socket，http是短连接
		close(s);

		//延时一会
		for(int countdown = 10; countdown >= 0; countdown--) {
			vTaskDelay(1000 / portTICK_PERIOD_MS);
		}
	}
}
void app_main()
{
	ESP_LOGI(TAG, "[APP] Startup..");
	ESP_LOGI(TAG, "\n\n-------------------------------- Get Systrm Info Start------------------------------------------\n");
	ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());//获取芯片可用内存
	ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());//获取IDF版本
	//获取从未使用过的最小内存
	ESP_LOGI(TAG, "[APP] esp_get_minimum_free_heap_size : %d  \n", esp_get_minimum_free_heap_size());
	//获取mac地址（station模式）
	uint8_t mac[6];
	esp_read_mac(mac, ESP_MAC_WIFI_STA);
	ESP_LOGI(TAG, "[APP] esp_read_mac(): %02x:%02x:%02x:%02x:%02x:%02x \n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	ESP_LOGI(TAG, "\n\n-------------------------------- Get Systrm Info End------------------------------------------\n");

	nvs_flash_init();
	wifi_init();
	while (1) {
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}