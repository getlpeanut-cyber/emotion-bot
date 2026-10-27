#include "asrpro.h"
#include "expressions.h"
#include "lvgl_v8_port.h"
#include <string.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "timer.h"

#define ASR_RX_BUF 256
#define LOL_SEARCH "wegame"   // 英雄联盟开始菜单搜索名(搜不到就改这里)   // 单行最大长度

static char g_rx[ASR_RX_BUF];
static int  g_rx_len = 0;

void asrpro_init(void)
{
    // RX=IO5, TX=IO4
    Serial1.begin(ASR_BAUD, SERIAL_8N1, ASR_RX_PIN, ASR_TX_PIN);
    Serial.println("[asr] uart init ok RX=5 TX=4 115200");   // 调试：确认串口已初始化
}

void asrpro_send(const char *line)
{
    if (!line) return;
    Serial1.print(line);
    Serial1.print('\n');
}

// 收到完整一行文本时调用：把 ASRPRO 下发的指令映射成动作
// 【示例】按 ASRPRO 实际发送的指令改这里
static void type_text(const char *s)
{
    while (*s) {
        Keyboard.write((uint8_t)*s++);
        delay(10);
    }
}

static void asrpro_handle_line(const char *line)
{
    Serial.printf("[asr] rx: %s\n", line);   // 调试用，USB CDC 可见

    if (strcmp(line, "wake") != 0) {
        wake_exit();   // wake 8s 内收到其它指令，提前退出
    }

    if (strcmp(line, "smile") == 0) {
        lvgl_port_lock(-1);
        expression_play("smile_stat_1");
        lvgl_port_unlock();
    } else if (strcmp(line, "music") == 0) {
        Keyboard.press(KEY_LEFT_CTRL);
        Keyboard.press(KEY_LEFT_ALT);
        Keyboard.press('p');
        delay(60);
        Keyboard.releaseAll();
        lvgl_port_lock(-1);
        expression_play("music_25s_1");
        lvgl_port_unlock();
    } else if (strcmp(line, "next") == 0) {
        Keyboard.press(KEY_LEFT_CTRL);
        Keyboard.press(KEY_LEFT_ALT);
        Keyboard.press(KEY_RIGHT_ARROW);
        delay(60);
        Keyboard.releaseAll();
    } else if (strcmp(line, "back") == 0) {
        Keyboard.press(KEY_LEFT_CTRL);
        Keyboard.press(KEY_LEFT_ALT);
        Keyboard.press(KEY_LEFT_ARROW);
        delay(60);
        Keyboard.releaseAll();
    } else if (strcmp(line, "screenshot") == 0) {
        Keyboard.press(KEY_LEFT_GUI);
        Keyboard.press(KEY_LEFT_SHIFT);
        Keyboard.press('s');
        delay(60);
        Keyboard.releaseAll();
    } else if (strcmp(line, "mute") == 0) {
        lvgl_port_lock(-1);
        expression_play("mute_05s_1");
        lvgl_port_unlock();
        ConsumerControl.press(CONSUMER_CONTROL_MUTE);
        delay(60);
        ConsumerControl.release();
    } else if (strcmp(line, "stop") == 0) {
        Keyboard.press(KEY_LEFT_CTRL);
        Keyboard.press(KEY_LEFT_ALT);
        Keyboard.press('p');
        delay(60);
        Keyboard.releaseAll();
    } else if (strcmp(line, "fight") == 0) {
        lvgl_port_lock(-1);
        expression_play_once("angry_20s_1");
        lvgl_port_unlock();
        // 按下 Win 键和 '3' 键
        Keyboard.press(KEY_LEFT_GUI);
        Keyboard.press('3');
        delay(60);
        Keyboard.releaseAll();
    } else if (strcmp(line, "poweroff") == 0) {
        lvgl_port_lock(-1);
        expression_play("leisure_05s_");
        lvgl_port_unlock();
        Keyboard.press(KEY_LEFT_GUI);   // Win+X
        Keyboard.press('x');
        delay(60);
        Keyboard.releaseAll();
        delay(250);
        Keyboard.press('u');          // U -> 关机或注销
        delay(60);
        Keyboard.releaseAll();
        delay(250);
        Keyboard.press('u');          // U -> 关机
        delay(60);
        Keyboard.releaseAll();
    } else if (strcmp(line, "wake") == 0) {
        wake_trigger();
    }else if (strcmp(line, "openwyy") == 0) {
        Keyboard.press(KEY_LEFT_GUI);
        Keyboard.press('4');
        delay(60);
        Keyboard.releaseAll();
    }else if (strcmp(line, "split") == 0) {
        Keyboard.press(KEY_LEFT_GUI);
        Keyboard.press(KEY_RIGHT_ARROW);
        Keyboard.releaseAll();
        delay(60);
        Keyboard.releaseAll();
    }else if (strcmp(line, "task") == 0) {
        Keyboard.press(KEY_LEFT_CTRL);
        Keyboard.press(KEY_LEFT_SHIFT);
        Keyboard.press(KEY_ESC);
        delay(60);
        Keyboard.releaseAll();
    }
    // 其它指令在此追加
}

void asrpro_poll(void)
{
    while (Serial1.available()) 
    {
        char c = (char)Serial1.read();
        Serial.printf("0x%02X ", (unsigned char)c);   // 调试：打印收到的原始字节

        if (c == '\n') 
        {
            // 收到行尾，处理这一行
            g_rx[g_rx_len] = '\0';
            if (g_rx_len > 0) {
                asrpro_handle_line(g_rx);
            }
            g_rx_len = 0;
        } else if (c == '\r') {
            // 忽略 \r，统一用 \n 作为行尾
        } else if (g_rx_len < ASR_RX_BUF - 1) {
            g_rx[g_rx_len++] = c;
        } else {
            // 缓冲区溢出：丢弃本行，重新开始
            g_rx_len = 0;
        }
    }
}
