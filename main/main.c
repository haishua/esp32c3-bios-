#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "bios_flasher.h"
#include "spi_flash.h"
#include "lwip/ip4_addr.h"

// WiFi + HTTP
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_http_server.h"

static httpd_handle_t server = NULL;
static uint32_t detected_flash_size = 0;
static esp_ip4_addr_t current_ip = {0};

static void print_usage(void) {
    printf("\r\n=== 操作完成 ===\r\n");
    printf("IP地址: " IPSTR "\r\n", IP2STR(&current_ip));
    printf("刷写BIOS: curl -# -X POST --data-binary @backup.bin http://" IPSTR "/stream/write\r\n", IP2STR(&current_ip));
    printf("验证BIOS: curl -# -X POST --data-binary @backup.bin http://" IPSTR "/stream/verify\r\n", IP2STR(&current_ip));
    printf("备份BIOS: curl -# -o backup.bin http://" IPSTR "/stream/backup\r\n", IP2STR(&current_ip));
    printf("=====================\r\n\r\n");
}

static void print_init_header(const char *warning_msg) {
    printf("\r\n=== ESP32C3 BIOS 刷写器 ===\r\n");
    printf("%s\r\n", warning_msg);
    detected_flash_size = 16 * 1024 * 1024;
}

static const char *TAG = "BIOS Flasher";

#define WIFI_SSID "ChinaNet-XfQxgA"
#define WIFI_PASS "dthy4324"

// HTTP 流式写入处理器 - 边接收边写SPI Flash
static esp_err_t stream_write_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    ESP_LOGI(TAG, "开始写入, 大小: %d bytes", req->content_len);
    printf("开始写入: %d bytes\r\n", req->content_len);

    uint8_t *buffer = malloc(4096);
    bool *erased_sectors = calloc(4096, sizeof(bool));
    if (!buffer || !erased_sectors) {
        free(buffer);
        free(erased_sectors);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "内存不足");
        return ESP_FAIL;
    }

    int received = 0;
    int remaining = req->content_len;
    esp_err_t ret = ESP_OK;

    if (remaining > detected_flash_size) {
        ESP_LOGE(TAG, "文件大小 %d 超过Flash大小 %u", remaining, detected_flash_size);
        remaining = detected_flash_size;
        ESP_LOGI(TAG, "限制写入大小为 %d bytes", remaining);
    }

    while (remaining > 0) {
        int to_recv = (remaining > 4096) ? 4096 : remaining;
        int ret_recv = httpd_req_recv(req, (char *)buffer, to_recv);
        if (ret_recv <= 0) {
            if (ret_recv == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "接收失败: %d", ret_recv);
            ret = ESP_FAIL;
            break;
        }

        size_t addr = received;
        size_t len = ret_recv;

        size_t sector_start = addr / SPI_FLASH_SECTOR_SIZE;
        size_t sector_end = (addr + len - 1) / SPI_FLASH_SECTOR_SIZE;

        for (size_t s = sector_start; s <= sector_end; s++) {
            if (!erased_sectors[s]) {
                ret = spi_flash_erase_sector(s * SPI_FLASH_SECTOR_SIZE);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "擦除扇区 %u 失败: %s", s, esp_err_to_name(ret));
                    free(buffer);
                    free(erased_sectors);
                    return ret;
                }
                erased_sectors[s] = true;
            }
        }

        size_t offset = 0;
        while (offset < len) {
            size_t current_addr = addr + offset;
            size_t page_boundary = ((current_addr / SPI_FLASH_PAGE_SIZE) + 1) * SPI_FLASH_PAGE_SIZE;
            size_t page_remain = page_boundary - current_addr;

            size_t page_len = len - offset;
            if (page_len > page_remain) page_len = page_remain;
            if (page_len > SPI_FLASH_PAGE_SIZE) page_len = SPI_FLASH_PAGE_SIZE;

            ret = spi_flash_write_page(current_addr, buffer + offset, page_len);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "写入页 0x%08X 失败: %s", current_addr, esp_err_to_name(ret));
                free(buffer);
                free(erased_sectors);
                return ret;
            }
            offset += page_len;
        }

        received += ret_recv;
        remaining -= ret_recv;
       
        printf("进度: %d / %d bytes (%u%%)\r\n", received, req->content_len, (received * 100) / req->content_len);
        fflush(stdout);
    }

    free(buffer);
    free(erased_sectors);
    ESP_LOGI(TAG, "写入完成: %d bytes", received);
    printf("写入完成: %d bytes\r\n", received);

    char resp[64];
    snprintf(resp, sizeof(resp), "写入成功: %d bytes\n", received);
    httpd_resp_sendstr(req, resp);
    print_usage();
    return ESP_OK;
}

// HTTP 流式备份处理器 - 读取SPI Flash并发送
static esp_err_t stream_backup_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "开始备份, 大小: %lu bytes", detected_flash_size);
    printf("开始备份: %lu bytes\r\n", detected_flash_size);

    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=bios_backup.bin");
    
    char content_len_str[32];
    snprintf(content_len_str, sizeof(content_len_str), "%lu", detected_flash_size);
    httpd_resp_set_hdr(req, "Content-Length", content_len_str);

    uint8_t *buffer = malloc(4096);
    if (!buffer) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "内存不足");
        return ESP_FAIL;
    }

    size_t total = detected_flash_size;
    size_t sent = 0;

    while (sent < total) {
        size_t to_send = (total - sent > 4096) ? 4096 : (total - sent);
        esp_err_t ret = spi_flash_read(sent, buffer, to_send);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "读取失败 0x%08X: %s", sent, esp_err_to_name(ret));
            free(buffer);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "读取错误");
            return ESP_FAIL;
        }

        if (httpd_resp_send_chunk(req, (char *)buffer, to_send) != ESP_OK) {
            ESP_LOGE(TAG, "发送失败");
            free(buffer);
            return ESP_FAIL;
        }

        sent += to_send;
        printf("进度: %zu / %zu bytes (%u%%)\r\n", sent, total, (sent * 100) / total);
        fflush(stdout);
    }

    httpd_resp_send_chunk(req, NULL, 0);
    free(buffer);

    ESP_LOGI(TAG, "备份完成: %lu bytes", sent);
    printf("备份完成: %zu bytes\r\n", sent);
    print_usage();
    return ESP_OK;
}

// HTTP 验证处理器 - 对比上传文件与Flash内容
static esp_err_t stream_verify_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    ESP_LOGI(TAG, "开始验证, 大小: %d bytes", req->content_len);
    printf("开始验证: %d bytes\r\n", req->content_len);

    uint8_t *buffer_file = malloc(4096);
    uint8_t *buffer_flash = malloc(4096);
    if (!buffer_file || !buffer_flash) {
        free(buffer_file);
        free(buffer_flash);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "内存不足");
        return ESP_FAIL;
    }

    int received = 0;
    int remaining = req->content_len;
    bool verify_ok = true;
    uint32_t first_error_addr = 0;
    uint8_t expected_byte = 0;
    uint8_t actual_byte = 0;

    if (remaining > detected_flash_size) {
        remaining = detected_flash_size;
    }

    while (remaining > 0) {
        int to_recv = (remaining > 4096) ? 4096 : remaining;
        int ret_recv = httpd_req_recv(req, (char *)buffer_file, to_recv);
        if (ret_recv <= 0) {
            if (ret_recv == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "接收失败: %d", ret_recv);
            verify_ok = false;
            break;
        }

        esp_err_t ret = spi_flash_read(received, buffer_flash, ret_recv);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "读取失败 0x%08X: %s", received, esp_err_to_name(ret));
            verify_ok = false;
            break;
        }

        for (int i = 0; i < ret_recv; i++) {
            if (buffer_file[i] != buffer_flash[i]) {
                verify_ok = false;
                first_error_addr = received + i;
                expected_byte = buffer_file[i];
                actual_byte = buffer_flash[i];
                ESP_LOGE(TAG, "验证失败 0x%08X: 期望 0x%02X, 实际 0x%02X",
                         first_error_addr, expected_byte, actual_byte);
                break;
            }
        }

        if (!verify_ok) break;

        received += ret_recv;
        remaining -= ret_recv;
        printf("进度: %d / %d bytes (%u%%)\r\n", received, req->content_len, (received * 100) / req->content_len);
        fflush(stdout);
    }

    free(buffer_file);
    free(buffer_flash);

    char resp[256];
    if (verify_ok) {
        snprintf(resp, sizeof(resp), "验证成功: %d bytes\n", received);
        ESP_LOGI(TAG, "验证通过: %d bytes", received);
        printf("验证通过: %d bytes\r\n", received);
        printf("验证成功！你可以上电开机了！\r\n");
    } else {
        snprintf(resp, sizeof(resp), "验证失败 0x%08" PRIx32 ": 期望 0x%02X, 实际 0x%02X\n", 
                 first_error_addr, expected_byte, actual_byte);
        ESP_LOGE(TAG, "验证失败 0x%08" PRIx32 ": 期望 0x%02X, 实际 0x%02X", 
                 first_error_addr, expected_byte, actual_byte);
        printf("验证失败 0x%08" PRIx32 ": 期望 0x%02X, 实际 0x%02X\r\n", 
               first_error_addr, expected_byte, actual_byte);
    }

    httpd_resp_sendstr(req, resp);
    print_usage();
    return ESP_OK;
}

// HTTP 首页
static esp_err_t root_handler(httpd_req_t *req) {
    char resp[1024];
    snprintf(resp, sizeof(resp),
        "ESP32C3 BIOS 刷写器\r\n"
        "==================\r\n"
        "Flash大小: %lu bytes\r\n"
        "\r\n"
        "命令:\r\n"
        "  备份: curl -# -o backup.bin http://<ip>/stream/backup\r\n"
        "  刷写: curl -# -X POST --data-binary @backup.bin http://<ip>/stream/write\r\n"
        "  验证: curl -# -X POST --data-binary @backup.bin http://<ip>/stream/verify\r\n",
        detected_flash_size);

    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

void start_http_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 4;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP服务器启动失败");
        return;
    }

    httpd_uri_t uri_root = {.uri = "/", .method = HTTP_GET, .handler = root_handler};
    httpd_uri_t uri_stream_write = {.uri = "/stream/write", .method = HTTP_POST, .handler = stream_write_handler};
    httpd_uri_t uri_stream_backup = {.uri = "/stream/backup", .method = HTTP_GET, .handler = stream_backup_handler};
    httpd_uri_t uri_stream_verify = {.uri = "/stream/verify", .method = HTTP_POST, .handler = stream_verify_handler};

    httpd_register_uri_handler(server, &uri_root);
    httpd_register_uri_handler(server, &uri_stream_write);
    httpd_register_uri_handler(server, &uri_stream_backup);
    httpd_register_uri_handler(server, &uri_stream_verify);

    ESP_LOGI(TAG, "HTTP服务器已启动，端口80");
}

// WiFi 事件处理
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi断开连接，正在重试...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        current_ip = event->ip_info.ip;
        ESP_LOGI(TAG, "WiFi连接成功! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        printf("\r\n=== WiFi 已连接 ===\r\n");
        printf("IP地址: " IPSTR "\r\n", IP2STR(&current_ip));
        printf("刷写BIOS: curl -# -X POST --data-binary @backup.bin http://" IPSTR "/stream/write\r\n", IP2STR(&current_ip));
        printf("验证BIOS: curl -# -X POST --data-binary @backup.bin http://" IPSTR "/stream/verify\r\n", IP2STR(&current_ip));
        printf("备份BIOS: curl -# -o backup.bin http://" IPSTR "/stream/backup\r\n", IP2STR(&current_ip));
        printf("=====================\r\n\r\n");
        start_http_server();
    }
}

void wifi_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi初始化完成，正在连接 %s...", WIFI_SSID);
}

void app_main(void) {
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // 初始化 NVS（WiFi 需要）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    printf("ESP32C3 BIOS 刷写器\r\n");
    printf("正在初始化...\r\n");
    vTaskDelay(100 / portTICK_PERIOD_MS);

    char info_buf[64];

    bios_flash_result_t result = bios_flasher_init();
    if (result == BIOS_FLASH_SUCCESS) {
        uint8_t manufacturer_id, device_id, capacity_id;
        result = bios_flasher_read_id_full(&manufacturer_id, &device_id, &capacity_id);
        if (result == BIOS_FLASH_SUCCESS) {
            detected_flash_size = spi_flash_get_size(manufacturer_id, device_id, capacity_id);
            printf("\r\n=== ESP32C3 BIOS 刷写器 ===\r\n");
            sprintf(info_buf, "Flash芯片: 厂商=0x%02X, 型号=0x%02X, 容量=%luMB\r\n",
                    manufacturer_id, device_id, (unsigned long)(detected_flash_size / (1024 * 1024)));
            printf("%s", info_buf);
        } else {
            print_init_header("警告: 读取Flash ID失败，默认使用16MB");
        }
    } else {
        print_init_header("警告: SPI初始化失败，默认使用16MB");
    }

    // 启动 WiFi
    wifi_init();
}
