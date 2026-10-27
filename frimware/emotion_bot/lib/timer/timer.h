#ifndef _timer_h_
#define _timer_h_



typedef unsigned char uchar;
typedef unsigned int uint;
typedef unsigned long ulong;

#define TIMER_NUM 4   //定时器个数
#define HWTIME 1      //最低频率为1ms

#define PIN     48   // 数据引脚
#define NUMPIX  1 // 灯珠数量

#define LED1 48

typedef struct 
{
    uint16_t time0;      // 修复：必须是16位，才能存下0xffff
    uint16_t reload_val; // 优化：记录重装载值，方便复位
    bool enabled;        // 优化：用独立的布尔值表示开启状态，不再依赖0xffff
} timerstr;

#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"

extern USBHIDKeyboard Keyboard;
extern USBHIDConsumerControl ConsumerControl; // USB HID 键盘,发 Ctrl+Alt+P 控制网易云
extern uint16_t g_random_tick;   // 500ms 计数, 60 次 = 30 秒


void T0_INIT();//定时器0初始化
void timer_function();//运行定时器
void inittimer();//软件定时器初始化
void random_expression_start(void);//上电/空闲随机播一个表情(内部自加 LVGL 锁)
void wake_trigger(void);// wake: play random wake expr, exit after 8s
void wake_exit(void);// wake 8s 内收到其它指令时提前退出


#endif
