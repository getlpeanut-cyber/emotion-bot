#include <Arduino.h>//调用Arduino库
#include <config.h>
#include "timer.h"
#include <ESP_Panel_Library.h>
#include "screen.h"
#include "lvgl_v8_port.h"
#include "lvgl.h"
#include "expressions.h"
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"

hw_timer_t *timer = NULL; //定义一个结构体指针
timerstr timersys[TIMER_NUM];
USBHIDKeyboard Keyboard;
USBHIDConsumerControl ConsumerControl; // 消费者控制:媒体静音键 // USB HID 键盘,发 Ctrl+Alt+P 控制网易云

// 优化1：必须加 volatile 防止编译器优化
volatile uint32_t Timer1msCount = 0;

// 空闲随机轮播：在三个表情里随机挑一个，每 30 秒重随机(音乐模式下暂停)
static bool g_music_on = false;      // false=随机轮播, true=music_25s
uint16_t g_random_tick = 0;   // 500ms 计数, 60 次 = 30 秒
static bool g_wake_on = false;      // wake overlay state
static uint16_t g_wake_tick = 0;    // 500ms tick, 16 = 8s

//Adafruit_NeoPixel pixels(NUMPIX, PIN, NEO_GRB  + NEO_KHZ800);


//	函数名称：onTimer()
//	函数功能：中断服务的功能，它必须是一个返回void（空）且没有输入参数的函数
//  为使编译器将代码分配到IRAM内，中断处理程序应该具有 IRAM_ATTR 属性
void IRAM_ATTR TimerEvent()
{
    Timer1msCount++;
}

// 优化2：合并初始化逻辑
void T0_INIT()
{
	timer = timerBegin(1000000);            // 频率 1MHz（1us 计数）
	timerAttachInterrupt(timer, &TimerEvent); // 去掉 "true"
	timerAlarm(timer, 1000, true, 0);       // 报警值 1000us，自动重载
}


// 软件定时器设置
void settimer(uint8_t timeno, uint16_t time1)
{
    if (timeno >= TIMER_NUM) return;
    timersys[timeno].reload_val = time1 / HWTIME;
    timersys[timeno].time0 = timersys[timeno].reload_val;
    timersys[timeno].enabled = true; // 开启定时器
}

void inittimer()//软件定时器初始化
{
	for(uint8_t i = 0; i < TIMER_NUM; i++)
	{
		timersys[i].time0 = 0;
		timersys[i].reload_val = 0;
		timersys[i].enabled = false;
	}
	settimer(0,1000);//1秒扫描一次
	settimer(1,500);//500ms扫描一次
	settimer(2,200);//200ms扫描一次
	settimer(3,100);//100ms扫描一次
}


// 定时器减1
void time_reduce()
{
    for(uint8_t i = 0; i < TIMER_NUM; i++)
    {
        if(timersys[i].enabled && timersys[i].time0 > 0)
        {
            timersys[i].time0--;
        }
    }
}

// 定时器时间到了
bool time_end(uint8_t timeno)
{
    if (timeno >= TIMER_NUM) return false;
    return (timersys[timeno].enabled && timersys[timeno].time0 == 0);
}




/*==============定时器需要刷新的程序===================*/


enum { BTN_IDLE = 0, BTN_DEBOUNCE_PRESS, BTN_HELD, BTN_DEBOUNCE_RELEASE };
#define BTN_DEBOUNCE_CNT 3   // 3 * 10ms = 30ms 消抖

// 随机播放三个表情之一(内部自加 LVGL 锁)
void random_expression_start(void)
{
    static const char *names[] = {"smile_stat_1", "sigh_20s_40s", "ponder_05s", "yummy_20_s", "investigat_1", "cry_10s_20_1", "asleep_215_1"};
    int idx = random(7);
    lvgl_port_lock(-1);
    expression_play(names[idx]);
    lvgl_port_unlock();
}

void wake_trigger(void)
{
    static const char *names[] = {"shocked_05s_", "question_05s", "shy_20s_40_1"};
    int idx = random(3);
    lvgl_port_lock(-1);
    expression_play(names[idx]);
    lvgl_port_unlock();
    g_wake_on = true;
    g_wake_tick = 0;
}

void wake_exit(void)
{
    if (!g_wake_on) return;
    g_wake_on = false;
    g_wake_tick = 0;
    if (g_music_on) {
        lvgl_port_lock(-1);
        expression_play("music_25s_1");
        lvgl_port_unlock();
    } else {
        g_random_tick = 0;
        random_expression_start();
    }
}

void music_control()
{
    static uint8_t state = BTN_IDLE;
    static uint8_t cnt = 0;

    bool pressed = (digitalRead(0) == LOW);

    switch (state) {
    case BTN_IDLE:
        if (pressed) { state = BTN_DEBOUNCE_PRESS; cnt = 0; }
        break;
    case BTN_DEBOUNCE_PRESS:
        if (pressed) {
            if (++cnt >= BTN_DEBOUNCE_CNT) {
                state = BTN_HELD;
                // 有效按下：发 Ctrl+Alt+P + 来回切换表情
                Keyboard.press(KEY_LEFT_CTRL);
                Keyboard.press(KEY_LEFT_ALT);
                Keyboard.press('p');
                delay(60);
                Keyboard.releaseAll();
                g_music_on = !g_music_on;
                g_wake_on = false;
                if (g_music_on) {
                    lvgl_port_lock(-1);
                    expression_play("music_25s_1");
                    lvgl_port_unlock();
                } else {
                    g_random_tick = 0;
                    random_expression_start();  // 回到随机模式，立即随机一个
                }
            }
        } else {
            state = BTN_IDLE;   // 抖动，回空闲
        }
        break;
    case BTN_HELD:
        if (!pressed) { state = BTN_DEBOUNCE_RELEASE; cnt = 0; }
        break;
    case BTN_DEBOUNCE_RELEASE:
        if (!pressed) {
            if (++cnt >= BTN_DEBOUNCE_CNT) state = BTN_IDLE;
        } else {
            state = BTN_HELD;   // 又按下了
        }
        break;
    }
}


void timerEvent0()
{
	//neopixelWrite(PIN, r, g, b)
	//music_control();
    settimer(0, 10); 
}
void timerEvent1()//500ms扫描一次(空闲时计数，30 秒重随机)
{
	if (g_wake_on) {
		g_wake_tick++;
		if (g_wake_tick >= 16) {
			wake_exit();
		}
	} else if (!g_music_on) {
		if (expression_is_static()) {
			g_random_tick = 0;   // 定格中，保持当前帧，不随机切换
		} else {
			g_random_tick++;
			if (g_random_tick >= 60) {  // 60 * 500ms = 30 秒
			g_random_tick = 0;
			random_expression_start();
			}
		}
	}
	settimer(1,500);//500ms扫描一次
}

void timerEvent2()
{
	settimer(2,200);//200ms扫描一次
}

void timerEvent3()
{
	settimer(3,100);//100ms扫描一次
}
//---------------------------------------

void timer_function()//运行定时器
{
	uchar i;
	
	while(Timer1msCount)
	{
		Timer1msCount--;
		time_reduce();//定时器减1
	}
	
	for(i=0;i<TIMER_NUM;i++)//有几个定时器循环几次
	{
		if(time_end(i))//判断那个定时器时间到了，就运行那个定时器对应的函数
		{
			switch(i)
			{
				case 0:
					timerEvent0();
					break;
				case 1:
					timerEvent1();
					break;
				case 2:
					timerEvent2();
					break;
				case 3:
					timerEvent3();
					break;
				default:break;
			}
		}
	}
	
	
}


