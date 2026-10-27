#include "eaf.h"

#include <string.h>
#include <stdlib.h>

#ifndef EAF_HOST_TEST
#include "esp_partition.h"
#include "cJSON.h"
#endif

// 块编码类型（与 gfx_eaf_dec 一致）
enum {
    EAF_ENC_RLE = 0,
    EAF_ENC_HUFFMAN_RLE = 1,
    EAF_ENC_HUFFMAN = 3,
    EAF_ENC_RAW = 5,
};

static inline uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// ---------------------------------------------------------------------------
// MMAP 容器
// ---------------------------------------------------------------------------
#ifndef EAF_HOST_TEST
bool eaf_assets_open(eaf_assets_t *a, const char *label)
{
    memset(a, 0, sizeof(*a));

    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
    if (!part) {
        return false;
    }

    const void *map = NULL;
    uint32_t handle = 0;   // spi_flash_mmap_handle_t 即 uint32_t
    esp_err_t err = esp_partition_mmap(part, 0, part->size,
                                       ESP_PARTITION_MMAP_DATA, &map, &handle);
    if (err != ESP_OK) {
        return false;
    }

    const uint8_t *d = (const uint8_t *)map;
    if (memcmp(d, "MMAP", 4) != 0) {
        esp_partition_munmap(handle);
        return false;
    }

    uint32_t name_len = rd32(d + 8);
    uint32_t count = rd32(d + 12);
    uint32_t pos = 32;                      // 容器头 32 字节
    uint32_t data_base = pos + count * (name_len + 12);

    if (count > 16) {
        count = 16;
    }
    for (uint32_t i = 0; i < count; i++) {
        a->names[i] = (const char *)(d + pos);
        uint32_t size = rd32(d + pos + name_len);
        uint32_t off = rd32(d + pos + name_len + 4);
        a->files[i] = d + data_base + off + 2;   // 跳过 "ZZ" 前缀
        a->file_sizes[i] = size;
        pos += name_len + 12;
    }
    a->file_count = (int)count;
    a->base = d;
    a->mmap_handle = (uint32_t)handle;
    return true;
}

void eaf_assets_close(eaf_assets_t *a)
{
    if (a->base && a->mmap_handle) {
        esp_partition_munmap((uint32_t)a->mmap_handle);
    }
    memset(a, 0, sizeof(*a));
}

// ---------------------------------------------------------------------------
// index.json 清单
// ---------------------------------------------------------------------------
int eaf_manifest_parse(const eaf_assets_t *a, eaf_manifest_entry_t *out, int max_out)
{
    int idx = -1;
    for (int i = 0; i < a->file_count; i++) {
        if (strcmp(a->names[i], "index.json") == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        return -1;
    }

    cJSON *root = cJSON_ParseWithLength((const char *)a->files[idx], a->file_sizes[idx]);
    if (!root || !cJSON_IsArray(root)) {
        if (root) {
            cJSON_Delete(root);
        }
        return -1;
    }

    int n = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (n >= max_out) {
            break;
        }
        eaf_manifest_entry_t *e = &out[n];
        memset(e, 0, sizeof(*e));

        cJSON *jname = cJSON_GetObjectItem(item, "name");
        cJSON *jfile = cJSON_GetObjectItem(item, "file");
        cJSON *jfps = cJSON_GetObjectItem(item, "fps");
        cJSON *jloop = cJSON_GetObjectItem(item, "loop");

        if (cJSON_IsString(jname) && jname->valuestring) {
            strncpy(e->name, jname->valuestring, sizeof(e->name) - 1);
        }
        if (cJSON_IsString(jfile) && jfile->valuestring) {
            strncpy(e->file, jfile->valuestring, sizeof(e->file) - 1);
        }
        e->fps = cJSON_IsNumber(jfps) ? (uint16_t)jfps->valueint : 25;
        if (e->fps == 0) {
            e->fps = 25;
        }

        e->loop_start = 0;
        e->loop_end = 0xFFFF;   // 默认到最后一帧
        if (cJSON_IsArray(jloop)) {
            int ls = cJSON_GetArraySize(jloop);
            cJSON *l0 = cJSON_GetArrayItem(jloop, 0);
            cJSON *l1 = cJSON_GetArrayItem(jloop, 1);
            if (ls == 1 && cJSON_IsNumber(l0)) {
                // loop:[N] -> 循环 0..N（不再定格）
                e->loop_start = 0;
                e->loop_end = (uint16_t)l0->valueint;
            } else if (ls >= 2 && cJSON_IsNumber(l0) && cJSON_IsNumber(l1)) {
                // loop:[a,b] -> 循环 a..b-1
                e->loop_start = (uint16_t)l0->valueint;
                e->loop_end = (uint16_t)(l1->valueint - 1);
            }
        }
        n++;
    }
    cJSON_Delete(root);
    return n;
}
#endif  // EAF_HOST_TEST

// ---------------------------------------------------------------------------
// EAF
// ---------------------------------------------------------------------------
bool eaf_anim_open(const eaf_assets_t *a, const char *filename, eaf_anim_t *anim)
{
    const uint8_t *eaf = NULL;
    for (int i = 0; i < a->file_count; i++) {
        if (strcmp(a->names[i], filename) == 0) {
            eaf = a->files[i];
            break;
        }
    }
    if (!eaf) {
        return false;
    }
    if (eaf[0] != 0x89 || memcmp(eaf + 1, "EAF", 3) != 0) {
        return false;
    }

    int total = (int)(int32_t)rd32(eaf + 4);
    anim->data = eaf;
    anim->total_frames = total;
    anim->frame_base = 16 + total * 8;

    // 读首帧尺寸
    uint32_t foff = rd32(eaf + 20);
    const uint8_t *f = eaf + anim->frame_base + foff + 2;  // 跳过 0x5A5A
    anim->width = rd16(f + 10);
    anim->height = rd16(f + 12);
    anim->loop_start = 0;
    anim->loop_end = (uint16_t)(total - 1);
    return true;
}

// ---------------------------------------------------------------------------
// 块解码
// ---------------------------------------------------------------------------
#define HUFF_MAX_NODES 512

typedef struct {
    int16_t left, right;
    uint8_t symbol;
    uint8_t leaf;
} hnode_t;

static hnode_t s_huff_nodes[HUFF_MAX_NODES];
static int s_huff_count;
static uint8_t *s_tmp = NULL;
static size_t s_tmp_cap = 0;

static int huff_node_new(void)
{
    if (s_huff_count >= HUFF_MAX_NODES) {
        return -1;
    }
    int n = s_huff_count++;
    s_huff_nodes[n].left = s_huff_nodes[n].right = -1;
    s_huff_nodes[n].symbol = 0;
    s_huff_nodes[n].leaf = 0;
    return n;
}

// in: [dict_size:u16][dict][encoded]，输出 symbols 到 out
static bool huffman_decode(const uint8_t *in, size_t in_size, uint8_t *out, size_t *out_size)
{
    if (in_size < 3) {
        return false;
    }
    uint16_t dict_size = (uint16_t)((in[1] << 8) | in[0]);
    if (in_size < 2 + (size_t)dict_size) {
        return false;
    }
    const uint8_t *dict = in + 2;
    const uint8_t *enc = in + 2 + dict_size;
    size_t enc_size = in_size - 2 - dict_size;

    s_huff_count = 0;
    int root = huff_node_new();
    if (root < 0) {
        return false;
    }

    if (enc_size == 0) {
        // 单色块：字典里只有一个符号
        size_t pos = 1;   // dict[0] 是 padding
        int symbol = 0, cnt = 0;
        while (pos < dict_size) {
            uint8_t sym = dict[pos];
            uint8_t cl = dict[pos + 1];
            pos += 2;
            size_t cbl = (cl + 7) / 8;
            if (pos + cbl > dict_size) {
                break;
            }
            pos += cbl;
            cnt++;
            symbol = sym;
            if (cnt > 1) {
                break;
            }
        }
        if (cnt == 1) {
            memset(out, symbol, *out_size);
            return true;
        }
        *out_size = 0;
        return true;
    }

    // 建树
    size_t pos = 1;
    while (pos < dict_size) {
        uint8_t symbol = dict[pos];
        uint8_t cl = dict[pos + 1];
        pos += 2;
        size_t cbl = (cl + 7) / 8;
        uint32_t code = 0;
        for (size_t i = 0; i < cbl; i++) {
            code = (code << 8) | dict[pos++];
        }
        int node = root;
        for (int bp = cl - 1; bp >= 0; bp--) {
            int bit = (code >> bp) & 1;
            int16_t *child = bit ? &s_huff_nodes[node].right : &s_huff_nodes[node].left;
            if (*child < 0) {
                *child = (int16_t)huff_node_new();
            }
            node = *child;
        }
        s_huff_nodes[node].leaf = 1;
        s_huff_nodes[node].symbol = symbol;
    }

    uint8_t padding = dict[0];
    size_t total_bits = enc_size * 8 - padding;
    size_t out_pos = 0;
    int node = root;
    for (size_t bi = 0; bi < total_bits && out_pos < *out_size; bi++) {
        int bit = (enc[bi >> 3] >> (7 - (bi & 7))) & 1;
        node = bit ? s_huff_nodes[node].right : s_huff_nodes[node].left;
        if (node < 0) {
            break;
        }
        if (s_huff_nodes[node].leaf) {
            out[out_pos++] = s_huff_nodes[node].symbol;
            node = root;
        }
    }
    *out_size = out_pos;
    return true;
}

static bool rle_decode(const uint8_t *in, size_t in_size, uint8_t *out, size_t *out_size)
{
    size_t ip = 0, op = 0;
    while (ip + 1 < in_size && op < *out_size) {
        uint8_t cnt = in[ip++];
        uint8_t val = in[ip++];
        for (uint8_t i = 0; i < cnt && op < *out_size; i++) {
            out[op++] = val;
        }
    }
    *out_size = op;
    return true;
}

// 解码一个块到 out（长度 out_size，8-bit 索引流）
static bool decode_block(const uint8_t *bd, int block_len, uint8_t *out, size_t out_size)
{
    uint8_t enc = bd[0];
    const uint8_t *payload = bd + 1;
    size_t plen = (size_t)(block_len - 1);
    size_t os = out_size;

    switch (enc) {
    case EAF_ENC_RLE:
        return rle_decode(payload, plen, out, &os);
    case EAF_ENC_HUFFMAN: {
        os = out_size;
        return huffman_decode(payload, plen, out, &os);
    }
    case EAF_ENC_HUFFMAN_RLE: {
        size_t tmp_sz = out_size * 2;
        if (s_tmp_cap < tmp_sz) {
            uint8_t *t = (uint8_t *)realloc(s_tmp, tmp_sz);
            if (!t) {
                return false;
            }
            s_tmp = t;
            s_tmp_cap = tmp_sz;
        }
        size_t tlen = tmp_sz;
        if (!huffman_decode(payload, plen, s_tmp, &tlen)) {
            return false;
        }
        os = out_size;
        return rle_decode(s_tmp, tlen, out, &os);
    }
    case EAF_ENC_RAW:
        if (plen > out_size) {
            plen = out_size;
        }
        memcpy(out, payload, plen);
        os = plen;
        return true;
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// 帧解码 + 最近邻缩放
// ---------------------------------------------------------------------------
bool eaf_decode_frame(const eaf_anim_t *anim, int frame_index,
                      uint8_t *out, int out_w, int out_h)
{
    const uint8_t *eaf = anim->data;
    uint32_t foff = rd32(eaf + 20 + frame_index * 8);
    const uint8_t *f = eaf + anim->frame_base + foff + 2;   // 跳过 0x5A5A

    if (f[0] != '_' || f[1] != 'S') {
        return false;
    }
    uint8_t bit_depth = f[9];
    if (bit_depth != 8) {
        return false;   // 仅支持 8-bit 调色板
    }
    int width = rd16(f + 10);
    int height = rd16(f + 12);
    int blocks = rd16(f + 14);
    int block_height = rd16(f + 16);

    const uint8_t *palette = f + 18 + blocks * 4;
    uint32_t data_offset = 18 + (uint32_t)blocks * 4 + 256 * 4;

    // 调色板 -> 大端 RGB565 两个字节
    uint8_t pal_hi[256], pal_lo[256];
    for (int i = 0; i < 256; i++) {
        uint8_t b = palette[i * 4];
        uint8_t g = palette[i * 4 + 1];
        uint8_t r = palette[i * 4 + 2];
        uint8_t r5 = r >> 3, g6 = g >> 2, b5 = b >> 3;
        pal_hi[i] = (uint8_t)((r5 << 3) | (g6 >> 3));
        pal_lo[i] = (uint8_t)(((g6 & 0x07) << 5) | b5);
    }

    // 块偏移
    uint32_t offsets[64];
    if (blocks > 64) {
        return false;
    }
    offsets[0] = data_offset;
    for (int i = 1; i < blocks; i++) {
        offsets[i] = offsets[i - 1] + rd32(f + 18 + (i - 1) * 4);
    }

    // 单块索引缓冲（懒解）
    int blk = width * block_height;
    static uint8_t *scratch = NULL;
    static int scratch_cap = 0;
    if (scratch_cap < blk) {
        uint8_t *s = (uint8_t *)realloc(scratch, (size_t)blk);
        if (!s) {
            return false;
        }
        scratch = s;
        scratch_cap = blk;
    }

    int decoded = -1;
    for (int y = 0; y < out_h; y++) {
        int sy = y * height / out_h;
        int b = sy / block_height;
        if (b != decoded) {
            const uint8_t *bdp = f + offsets[b];
            int blen = (int)rd32(f + 18 + b * 4);
            if (!decode_block(bdp, blen, scratch, (size_t)blk)) {
                return false;
            }
            decoded = b;
        }
        int ry = sy - b * block_height;
        const uint8_t *row = scratch + ry * width;
        uint8_t *dst = out + (size_t)y * out_w * 2;
        for (int x = 0; x < out_w; x++) {
            int sx = x * width / out_w;
            uint8_t ci = row[sx];
            dst[x * 2] = pal_hi[ci];
            dst[x * 2 + 1] = pal_lo[ci];
        }
    }
    return true;
}
