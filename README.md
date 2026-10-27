# 🤖 Emotion-Bot：基于 ESP32-S3 与 ASRPRO 的桌面 AI 萌宠

<p align="center">
  <img src="docs/preview.gif" alt="Emotion-Bot Preview" width="400">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/ESP32--S3-N16R8-blue?logo=espressif" alt="ESP32">
  <img src="https://img.shields.io/badge/LVGL-v8.4-green" alt="LVGL">
  <img src="https://img.shields.io/badge/ASRPRO-Offline_Voice-orange" alt="ASRPRO">
  <img src="https://img.shields.io/badge/License-MIT-purple" alt="License">
</p>

## 📖 项目简介
**Emotion-Bot** 是一个软硬全栈自研的桌面小机器人。它拥有一块 1.85 寸的圆形屏幕（ST77916 驱动），能显示基于 LVGL 8.4 的生动动态表情；内置 ASRPRO 离线语音识别模块，无需联网即可通过语音指令切换表情；同时，ESP32-S3 模拟 USB HID 设备，可通过语音直接反向控制电脑（网易云播放/暂停、系统静音、截图等）。
演示视频：https://www.bilibili.com/video/BV1H9xM68E49/?spm_id_from=333.1387.homepage.video_card.click&vd_source=84def0de4bb5678fbc35bdef742d9e0a

---

## ✨ 核心功能
*   🗣️ **离线语音交互**：基于 ASRPRO 2.0，唤醒词自定义，断网也能精准识别指令。
*   😊 **动态表情系统**：使用天问 Block 配合串口发送指令，ESP32 端基于 LVGL 渲染动态表情。
*   💻 **PC 反向控制**：ESP32 模拟键盘/多媒体键，语音喊“静音/播放音乐/打开游戏”，电脑自动执行。

---

## 🛠️ 硬件架构 (Hardware)

### 核心元器件 (BOM)
| 模块 | 型号/规格 | 说明 |
| :--- | :--- | :--- |
| 主控 | ESP32-S3-WROOM-1-N16R8 | 16M Flash, 8M PSRAM，负责屏幕驱动与 USB HID |
| 语音 | ASRPRO 2.0 | 离线语音识别，通过 UART 串口与主控通信 |
| 屏幕 | 1.85寸 圆形 IPS 屏 | 360x360 分辨率，ST77916 驱动，QSPI 接口，39 PIN FPC |
| 发声 | 16mm 8Ω 1W 喇叭 | 连接 ASRPRO 播放反馈音 |
| 稳压 | 470uF 贴片铝电解 + 100nF 防止喇叭大电流导致系统重启 |

*   👉 **[点击下载原理图 PDF](hardware/schematic.pdf)**
*   👉 **[点击下载 Gerber 打板文件](hardware/pcb_gerber.zip)**
*   👉 **[点击下载 3D 打印外壳模型](hardware/3d_model.step)**

---

## 💻 软件架构 (Software)

### 1. ESP32-S3 端 (PlatformIO)
*   **UI 框架**：LVGL 8.4.0
*   **屏幕驱动**：`ESP32_Display_Panel`
*   **HID 控制**：内置 `USBHIDKeyboard` 与 `USBHIDConsumerControl`
*   **核心逻辑**：通过 `Serial1` 接收 ASRPRO 发来的字符串（如 `"angry\n"`），使用 `lv_async_call` 安全切换表情，同时模拟键盘发送快捷键。

### 2. ASRPRO 端 (天问 Block)
*   配置串口1（`PA2` 为 TX，`PA3` 为 RX，波特率 `115200`）。
*   在识别到对应命令词时，通过串口发送命令给 ESP32。

---

## 🤝 贡献与致谢
欢迎提交 Issue 或 PR！如果这个项目帮到了你，请给个 ⭐ Star 支持一下！

## 📜 开源协议
本项目采用 [CC BY-NC-SA 4.0 (署名-非商业性使用-相同方式共享)](LICENSE) 协议。
**允许**：个人学习、修改、非商业分发。
**禁止**：任何形式的商业用途（包括但不限于销售、商业产品集成等）。
