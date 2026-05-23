# ESP32C3 BIOS 刷写器

基于 ESP32C3 的 SPI Flash 刷写工具，支持通过 HTTP 进行 BIOS 的备份、刷写和验证操作。

## 功能特性

- ✅ **BIOS 备份** - 通过 HTTP 下载 SPI Flash 内容
- ✅ **BIOS 刷写** - 通过 HTTP 上传并写入 SPI Flash
- ✅ **BIOS 验证** - 对比上传文件与 Flash 内容
- ✅ **实时进度显示** - 串口实时显示操作进度
- ✅ **无需前端** - 直接使用 curl 命令操作

## 硬件连接

| ESP32C3 引脚 | SPI Flash 引脚 |
|--------------|---------------|
| GPIO1 (MOSI) | DI / SI       |
| GPIO2 (CLK)  | CLK           |
| GPIO13 (MISO)| DO / SO       |
| GPIO12 (CS)  | CS#           |
| 3.3V         | VCC, WP#, HOLD# |
| GND          | GND           |

> ⚠️ 注意：请使用 SOIC 夹子或正确焊接 BIOS 芯片，仅使用 3.3V 电压！

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
2. 刷写前建议先备份原有 BIOS
3. 验证通过后再上电测试
4. Windows 用户请使用 CMD 或 PowerShell 的 curl.exe

## 许可证

MIT License
