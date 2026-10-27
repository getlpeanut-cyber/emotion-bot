#include "config.h"
#include "timer.h"
#include <ESP_Panel_Library.h>
#include "screen.h"
#include "lvgl_v8_port.h"
#include "lvgl.h"
#include "expressions.h"
#include "asrpro.h"
#include "USB.h"
#include "USBHIDKeyboard.h"

// RGB 灯引脚，按实际接线修改
#define RGB_LED_PIN 48

// 全局变量
float adc_vol = 0;

void io_init() // IO 初始化
{
    // pinMode(LED1, OUTPUT);
    pinMode(0, INPUT_PULLUP); // BOOT 键(GPIO0)作音乐控制按键
}

void soft_init() // 软件初始化
{
    inittimer();   // 初始化软件定时器
    screen_init(); // 初始化屏幕（内部会打开背光）

    // 接入 LVGL 显示驱动（触摸暂不接）
    if (lvgl_port_init(screen_get_lcd(), nullptr)) {
        lvgl_port_lock(-1);
        expression_init();
        lvgl_port_unlock();
        random_expression_start();   // 上电随机播一个(内部自加锁)
    } else {
        Serial.println("Error: LVGL port init failed!");
    }

    neopixelWrite(RGB_LED_PIN, 0, 0, 0);
}

void port_init() // 硬件初始化
{
    Serial.begin(115200); // 设置波特率为 115200
    Keyboard.begin();     // 注册 USB HID 键盘接口
    ConsumerControl.begin(); // 注册消费者控制(媒体静音键)
    USB.begin();          // 启动 USB(CDC+HID 复合设备)
    T0_INIT();            // 定时器 0 初始化
    asrpro_init();       // 初始化 ASRPRO 串口(IO5=RX, IO4=TX)
}

void setup()
{
    port_init();  // 硬件初始化
    io_init();    // IO 初始化
    soft_init();  // 软件初始化
}


void loop()
{
    timer_function(); // 主循环定时器
    asrpro_poll();    // 轮询 ASRPRO 串口(双向通信)
}
