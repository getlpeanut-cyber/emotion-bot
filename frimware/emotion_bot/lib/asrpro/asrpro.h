#ifndef ASRPRO_H
#define ASRPRO_H

#include <Arduino.h>

// ASRPRO 语音模块串口(IO5=RX, IO4=TX, 115200 8N1, 文本行以 \n 结尾)
#define ASR_RX_PIN 5
#define ASR_TX_PIN 4
#define ASR_BAUD   115200

// 初始化串口(在 port_init 里调用一次)
void asrpro_init(void);

// 非阻塞轮询：读取并解析文本行，收到完整一行后交给内部处理
// 需要放在主循环里频繁调用
void asrpro_poll(void);

// 向 ASRPRO 发送一行文本(自动追加 \n)
void asrpro_send(const char *line);

#endif
