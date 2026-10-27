#ifndef EXPRESSIONS_H
#define EXPRESSIONS_H

// 表情播放 API（运行时解码 emote-assets.bin 里的 .eaf，见 eaf.h/eaf.c）

// 初始化表情播放器：打开 assets 分区、解析清单、建全屏图片对象。
// 需在 LVGL 就绪后调用（即 lvgl_port_init 之后）。
void expression_init(void);

// 播放指定名字的表情（如 "angry_20s" / "smile_static"），找不到返回 false。
bool expression_play(const char *name);
bool expression_play_once(const char *name);   // 播一遍后定格最后一帧
bool expression_is_static(void);               // 是否定格中(锁空闲随机)

// 轮播：按清单顺序，每个表情播 5 秒后切到下一个，循环播放。
void expression_rotate_start(void);

#endif
