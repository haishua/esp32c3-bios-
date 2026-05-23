# ESP32C3 BIOS 刷写器

基于 ESP32C3 的 SPI Flash 刷写工具，支持通过 HTTP 进行 BIOS 的备份、刷写和验证操作
用于临时刷写电脑的bios文件
没有前端，直接用curl命令来流备份和写入
懒得弄前端了，太麻烦了，
之前尝试弄了前端，但是浏览器总是会自动添加 boundary 分隔符 和文件元数据导致写进去的数据不一样
所以......就这样


## 功能

**BIOS 备份** - 通过 HTTP 下载 SPI Flash 内容
**BIOS 刷写** - 通过 HTTP 上传并写入 SPI Flash
**BIOS 验证** - 对比上传文件与 Flash 内容


## 硬件连接

| ESP32C3 引脚 | SPI Flash 引脚 |
|--------------|---------------|
| GPIO1 (MOSI) | DI / SI       |
| GPIO2 (CLK)  | CLK           |
| GPIO13 (MISO)| DO / SO       |
| GPIO12 (CS)  | CS#           |
| 3.3V         | VCC, WP#, HOLD# |
| GND          | GND           |



> 也可以自己在spi_flash.c里改
> 注意：请使用 SOIC 夹子或正确焊接 BIOS 芯片，仅使用 3.3V 电压！

## 软件配置

### WiFi 设置

在 `main/main.c` 中修改 WiFi 配置：

```c
#define WIFI_SSID "你的WiFi名称"
#define WIFI_PASS "你的WiFi密码"
```

## 编译与烧录

```bash
# 设置 ESP-IDF 环境
export IDF_PATH=/path/to/esp-idf
. $IDF_PATH/export.sh

# 配置项目
idf.py set-target esp32c3

# 编译
idf.py build

# 烧录（替换 COMx 为你的串口）
idf.py -p COMx flash monitor
```

## 使用方法

设备启动后会自动连接 WiFi 并显示 IP 地址，使用以下 curl 命令操作：

### 1. 备份 BIOS

```bash
curl -# -o bios_backup.bin http://<设备IP>/stream/backup
```

### 2. 刷写 BIOS

```bash
curl -# -X POST --data-binary @bios_backup.bin http://<设备IP>/stream/write -o response.txt
```

### 3. 验证 BIOS

```bash
curl -# -X POST --data-binary @bios_backup.bin http://<设备IP>/stream/verify -o response.txt
```

## 串口输出示例

```
=== ESP32C3 BIOS 刷写器 ===
Flash芯片: 厂商=0xEF, 型号=0x40, 容量=4MB

=== WiFi 已连接 ===
IP地址: 192.168.1.91
刷写BIOS: curl -# -X POST --data-binary @bios_backup.bin http://192.168.1.91/stream/write -o response.txt
验证BIOS: curl -# -X POST --data-binary @bios_backup.bin http://192.168.1.91/stream/verify -o response.txt
备份BIOS: curl -# -o bios_backup.bin http://192.168.1.91/stream/backup
=====================
```

## API 接口

| 端点 | 方法 | 功能 |
|------|------|------|
| `/` | GET | 首页帮助信息 |
| `/stream/backup` | GET | 下载 Flash 内容 |
| `/stream/write` | POST | 写入 Flash |
| `/stream/verify` | POST | 验证 Flash 内容 |

## 注意事项

1. 确保 BIOS 芯片已正确连接
2. 刷写前一点要备份原有 BIOS
3. 验证通过后再上电测试
4. Windows 用户要使用 CMD 

