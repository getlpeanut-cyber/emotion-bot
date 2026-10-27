#include "expressions.h"
#include "eaf.h"

#include <Arduino.h>
#include <lvgl.h>
#include <string.h>
#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_partition.h"

#define OUT_W 360
#define OUT_H 360

static eaf_assets_t g_assets;
static eaf_manifest_entry_t g_manifest[16];
static int g_manifest_count = 0;

static lv_obj_t *g_img = nullptr;
static lv_timer_t *g_timer = nullptr;
static bool g_play_once = false;   // 播一遍后定格最后一帧
static bool g_static_hold = false; // 定格中：锁住空闲随机切换

static eaf_anim_t g_anim;
static int g_frame = 0;
static int g_fps = 25;

static int g_hold_frame = -1;      // 需要停留的帧索引(-1=不停留)
static int g_hold_ms = 0;          // 停留毫秒
static bool g_holding = false;     // 是否正在停留
static int g_loop_start_after = 0; // 第一遍结束后循环使用的 loop_start
static bool g_first_pass = true;   // 是否第一遍(用于动态 loop_start)

static uint8_t *g_fb = nullptr;         // OUT_W*OUT_H*2 大端 RGB565（PSRAM）
static lv_img_dsc_t g_dsc;

void expression_init()
{
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "assets");
    if (part) Serial.printf("[expr] assets partition size = %u (0x%X)\n", (unsigned)part->size, (unsigned)part->size);
    // 打开 assets 分区并解析表情清单
    if (!eaf_assets_open(&g_assets, "assets")) {
        Serial.println("[expr] assets partition open failed");
    } else {
        g_manifest_count = eaf_manifest_parse(&g_assets, g_manifest, 16);
        Serial.printf("[expr] loaded %d expressions\n", g_manifest_count);
    }

    // 帧缓冲放 PSRAM（360*360*2 = 259KB，内部 RAM 放不下）
    g_fb = (uint8_t *)heap_caps_malloc(OUT_W * OUT_H * 2, MALLOC_CAP_SPIRAM);
    if (!g_fb) {
        g_fb = (uint8_t *)malloc(OUT_W * OUT_H * 2);
    }

    memset(&g_dsc, 0, sizeof(g_dsc));
    g_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    g_dsc.header.w = OUT_W;
    g_dsc.header.h = OUT_H;
    g_dsc.data_size = OUT_W * OUT_H * 2;
    g_dsc.data = g_fb;

    // 全屏黑底 + 居中图片对象
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);

    g_img = lv_img_create(lv_scr_act());
    lv_obj_center(g_img);
    lv_obj_add_flag(g_img, LV_OBJ_FLAG_HIDDEN);
}

static void frame_advance(lv_timer_t *t)
{
    g_frame++;
    if (g_frame > (int)g_anim.loop_end) {
        if (g_play_once) {
            // 播到最后：定格最后一帧并停掉定时器
            g_frame = g_anim.loop_end;
            if (g_timer) {
                lv_timer_del(g_timer);
                g_timer = nullptr;
            }
            return;
        }
        // 循环到尾：第一遍结束后切换 loop_start(如 music_25s 从 0 变 9)
        if (g_first_pass) {
            g_first_pass = false;
            g_anim.loop_start = g_loop_start_after;
        }
        g_frame = g_anim.loop_start;
    }

    if (!eaf_decode_frame(&g_anim, g_frame, g_fb, OUT_W, OUT_H)) {
        return;
    }
    lv_img_set_src(g_img, &g_dsc);

    // 该帧需要停留(如 smile_static 第二帧停 500ms)
    if (g_frame == g_hold_frame && g_hold_ms > 0) {
        g_holding = true;
        lv_timer_set_period(t, g_hold_ms);
    }
}

static void frame_timer_cb(lv_timer_t *t)
{
    if (g_holding) {
        // 停留结束，恢复帧率后继续前进
        g_holding = false;
        lv_timer_set_period(t, 1000 / g_fps);
    }
    frame_advance(t);
}

static bool expression_play_impl(const char *name)
{
    if (!g_img || !g_fb) {
        return false;
    }

    // 在清单里找名字
    int mi = -1;
    for (int i = 0; i < g_manifest_count; i++) {
        if (strcmp(g_manifest[i].name, name) == 0) {
            mi = i;
            break;
        }
    }
    if (mi < 0) {
        Serial.printf("[expr] name not found: %s\n", name);
        return false;
    }

    if (!eaf_anim_open(&g_assets, g_manifest[mi].file, &g_anim)) {
        Serial.printf("[expr] anim open failed: %s\n", name);
        return false;
    }
    g_anim.loop_start = g_manifest[mi].loop_start;
    g_anim.loop_end = g_manifest[mi].loop_end;
    if (g_anim.loop_end == 0xFFFF || g_anim.loop_end >= (uint16_t)g_anim.total_frames) {
        g_anim.loop_end = (uint16_t)(g_anim.total_frames - 1);
    }
    g_fps = g_manifest[mi].fps;

    // 每个表情的播放配置
    g_hold_frame = -1;
    g_hold_ms = 0;
    g_holding = false;
    g_loop_start_after = 0;
    g_first_pass = true;
    g_static_hold = g_play_once;   // 播一次时锁空闲随机，直到下次播放

    if (strcmp(name, "smile_static") == 0 || strcmp(name, "smile_stat_1") == 0) {
        // smile_static：第二帧(索引1)停留 500ms
        g_hold_frame = 1;
        g_hold_ms = 500;
    } else if (strcmp(name, "music_25s") == 0 || strcmp(name, "music_25s_1") == 0) {
        // music_25s：第一次播放到 53 帧，之后循环 9~53 帧
        if (52 < (int)g_anim.total_frames) {
            g_anim.loop_end = 52;
        }
        g_loop_start_after = 9;
    }

    if (g_play_once) {
        // 播一次：忽略素材 loop，从头播到最后一帧后定格
        g_anim.loop_start = 0;
        g_anim.loop_end = (uint16_t)(g_anim.total_frames - 30);//倒数前30帧
    }

    if (g_timer) {
        lv_timer_del(g_timer);
        g_timer = nullptr;
    }

    g_frame = 0;
    if (!eaf_decode_frame(&g_anim, 0, g_fb, OUT_W, OUT_H)) {
        Serial.printf("[expr] frame0 decode failed: %s\n", name);
        return false;
    }
    lv_img_set_src(g_img, &g_dsc);

    if (g_anim.total_frames > 1) {
        // 逐帧播放，周期来自 fps
        g_timer = lv_timer_create(frame_timer_cb, 1000 / g_fps, nullptr);
    }

    lv_obj_clear_flag(g_img, LV_OBJ_FLAG_HIDDEN);
    return true;
}

bool expression_play(const char *name)
{
    g_play_once = false;
    return expression_play_impl(name);
}

bool expression_play_once(const char *name)
{
    g_play_once = true;
    return expression_play_impl(name);
}

bool expression_is_static(void)
{
    return g_static_hold;
}

// ---------------------------------------------------------------------------
// 轮播：按清单顺序，每个表情播 5 秒后切到下一个，循环
// ---------------------------------------------------------------------------
#define ROTATE_MS 5000

static int g_rotate_index = 0;
static lv_timer_t *g_rotate_timer = nullptr;

static void rotate_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (g_manifest_count <= 0) 
    {
        return;
    }
    g_rotate_index = (g_rotate_index + 1) % g_manifest_count;
    expression_play(g_manifest[g_rotate_index].name);
}

void expression_rotate_start()
{
    if (g_manifest_count <= 0) {
        return;
    }
    g_rotate_index = 0;
    expression_play(g_manifest[0].name);   // 先放第一个

    if (g_rotate_timer) {
        lv_timer_del(g_rotate_timer);
        g_rotate_timer = nullptr;
    }
    g_rotate_timer = lv_timer_create(rotate_timer_cb, ROTATE_MS, nullptr);
}
