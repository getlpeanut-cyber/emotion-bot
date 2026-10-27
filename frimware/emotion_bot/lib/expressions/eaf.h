#ifndef EAF_H
#define EAF_H

/*
 * 运行时 .eaf 解码器（移植自 ESP_GFX 的 gfx_eaf_dec.c，格式一致）。
 *
 * 职责：
 *   - 打开 flash 里的 emote-assets.bin（MMAP 容器，放在名为 "assets" 的分区），
 *     直接 mmap，不拷贝到 RAM；
 *   - 解析 index.json 清单（表情名 / 文件名 / loop 区间 / fps）；
 *   - 按需把某一 .eaf 的某一帧解码成 360x360 大端 RGB565。
 *
 * 仅支持本素材包所用的 8-bit 调色板帧 + RLE/Huffman 块编码。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 资产集（MMAP 容器）
typedef struct {
    const uint8_t *base;             // mmap 基址（容器头）
    uint32_t mmap_handle;            // 用于 esp_partition_munmap
    int file_count;
    const char *names[16];           // 存储文件名
    const uint8_t *files[16];        // 各文件内容指针
    uint32_t file_sizes[16];         // 各文件大小
} eaf_assets_t;

// 一个已解析的动画（指向 mmap 中的 .eaf 数据，不拷贝）
typedef struct {
    const uint8_t *data;             // .eaf 内容
    int total_frames;
    int frame_base;                  // 帧数据区起始偏移 = 16 + total*8
    int width, height;               // 首帧尺寸
    uint16_t loop_start;             // 循环段起点（含）
    uint16_t loop_end;               // 循环段终点（含）
} eaf_anim_t;

// index.json 里的一个表情条目（loop 已归一化为含端点）
typedef struct {
    char name[24];
    char file[24];
    uint16_t loop_start;
    uint16_t loop_end;               // 0xFFFF 表示“到最后一帧”
    uint16_t fps;
} eaf_manifest_entry_t;

// 打开 assets 分区并解析 MMAP 容器
bool eaf_assets_open(eaf_assets_t *a, const char *partition_label);
void eaf_assets_close(eaf_assets_t *a);

// 解析 index.json 清单，返回条目数（失败返回 -1）
int eaf_manifest_parse(const eaf_assets_t *a, eaf_manifest_entry_t *out, int max_out);

// 按文件名打开动画，读取总帧数/尺寸，loop 用调用方随后覆盖
bool eaf_anim_open(const eaf_assets_t *a, const char *filename, eaf_anim_t *anim);

// 解码第 frame_index 帧，缩放(最近邻)写入 out（out_w*out_h*2 字节，大端 RGB565）
bool eaf_decode_frame(const eaf_anim_t *anim, int frame_index,
                      uint8_t *out, int out_w, int out_h);

#ifdef __cplusplus
}
#endif

#endif
