#!/usr/bin/env python3
# -*- coding: gbk -*-
"""
将 Espressif emote-assets.bin (MMAP 容器 + EAF 帧) 解包为 LVGL RGB565 帧表。

这是"路线 B"的解包器: 在构建期把 .eaf 的每一帧解出来(8-bit 调色板 + RLE/Huffman
块编码), 缩放后转成与 gif_to_lvgl.py 完全一致的大端 RGB565 帧表, 并附带
loop 区间信息(来自 index.json)。

用法示例:
  python eaf_unpack.py --bin data/emote-assets.bin --out lib/expressions --size 360 \
      --expr angry_20s --max-frames 14

  python eaf_unpack.py --dump-png _png  --expr angry_20s     # 只解出 PNG 用于目检

格式说明(与 ESP_GFX 的 gfx_eaf_dec 一致):
  MMAP 头: 32 字节 (magic "MMAP", version, name_len=20, count, idx_size, data_size, 8 保留),
           后接 count*32 字节文件表 (name[20] + size + offset + flags),
           数据区 base=352, 每个存储文件前有 2 字节 "ZZ"(0x5A5A) 前缀。
  EAF 头: 0x89 "EAF" + total_frames(int32) + checksum(uint32) + table_len(uint32),
           后接 total_frames*8 字节帧表 (frame_size + frame_offset)。
  每帧: 0x5A5A 魔数 + "_S" 头, 头内 bit_depth(8)/width/height/blocks/block_height,
       block_len 表(每块 u32) + 256 色 RGBA 调色板, 之后是各块数据。
  块编码: byte0=类型 (0=RLE, 1=Huffman+RLE, 3=Huffman直接, 5=RAW)。
"""
import argparse
import json
import os
import struct

from PIL import Image

BYTES_PER_LINE = 12

DEFAULT_BIN = "d:/project_entire/project/emotion_bot/data/emote-assets.bin"


# --------------------------------------------------------------------------
# MMAP 容器
# --------------------------------------------------------------------------
def parse_mmap(data):
    assert data[0:4] == b"MMAP", "不是 MMAP 容器"
    version, name_len, count, idx_size, data_size = struct.unpack_from("<IIIII", data, 4)
    entries = []
    pos = 32  # 头 32 字节
    for _ in range(count):
        name = data[pos:pos + name_len].split(b"\0")[0].decode("utf-8", "replace")
        size, off, flags = struct.unpack_from("<III", data, pos + name_len)
        entries.append((name, size, off, flags))
        pos += name_len + 12
    data_base = pos
    files = {}
    for name, size, off, flags in entries:
        # 存储文件 = "ZZ"(2字节) + 内容, off 指向 "ZZ" 起点
        files[name] = data[data_base + off + 2: data_base + off + 2 + size]
    return files


# --------------------------------------------------------------------------
# EAF
# --------------------------------------------------------------------------
def parse_eaf(content):
    assert content[0] == 0x89 and content[1:4] in (b"EAF", b"AAF"), "不是 EAF 文件"
    total = struct.unpack_from("<i", content, 4)[0]
    checksum = struct.unpack_from("<I", content, 8)[0]
    table_len = struct.unpack_from("<I", content, 12)[0]
    table = [struct.unpack_from("<II", content, 16 + i * 8) for i in range(total)]
    frame_base = 16 + total * 8
    return total, table, frame_base


def parse_frame_header(fd):
    """fd: 单帧原始字节(含 0x5A5A 魔数)."""
    assert fd[0:2] == b"\x5a\x5a", "帧魔数错误"
    f = fd[2:]
    assert f[0:2] == b"_S", "帧格式非 _S"
    bit_depth = f[9]
    width = struct.unpack_from("<H", f, 10)[0]
    height = struct.unpack_from("<H", f, 12)[0]
    blocks = struct.unpack_from("<H", f, 14)[0]
    block_height = struct.unpack_from("<H", f, 16)[0]
    block_lens = [struct.unpack_from("<I", f, 18 + i * 4)[0] for i in range(blocks)]
    num_colors = (1 << bit_depth) if bit_depth in (4, 8) else 0
    pal_off = 18 + blocks * 4
    palette = f[pal_off:pal_off + num_colors * 4]  # 每色 RGBA 4 字节
    data_offset = pal_off + num_colors * 4
    return {
        "bit_depth": bit_depth, "width": width, "height": height,
        "blocks": blocks, "block_height": block_height,
        "block_lens": block_lens, "palette": palette,
        "num_colors": num_colors, "data_offset": data_offset, "file_data": f,
    }


def decode_rle(in_data, out_size):
    out = bytearray()
    pos = 0
    n = len(in_data)
    while pos + 1 < n and len(out) < out_size:
        count = in_data[pos]
        val = in_data[pos + 1]
        pos += 2
        out.extend(bytes([val]) * count)
    return bytes(out[:out_size])


def decode_huffman(payload, out_size):
    dict_size = struct.unpack_from("<H", payload, 0)[0]
    dict_data = payload[2:2 + dict_size]
    encoded = payload[2 + dict_size:]

    if not encoded:
        # 单色块: 字典里只有一个符号, 数据长度为 0
        pos = 1  # dict[0] 是 padding
        symbol = None
        cnt = 0
        while pos < len(dict_data):
            sym = dict_data[pos]
            cl = dict_data[pos + 1]
            pos += 2
            cbl = (cl + 7) // 8
            if pos + cbl > len(dict_data):
                break
            pos += cbl
            cnt += 1
            symbol = sym
            if cnt > 1:
                break
        if cnt == 1:
            return bytes([symbol]) * out_size
        return bytes(out_size)

    padding = dict_data[0]
    root = {}
    pos = 1
    while pos < len(dict_data):
        symbol = dict_data[pos]
        code_len = dict_data[pos + 1]
        pos += 2
        code_byte_len = (code_len + 7) // 8
        code = 0
        for _ in range(code_byte_len):
            code = (code << 8) | dict_data[pos]
            pos += 1
        node = root
        for bit_pos in range(code_len - 1, -1, -1):
            bit = (code >> bit_pos) & 1
            node = node.setdefault(bit, {})
        node["leaf"] = symbol

    out = bytearray()
    total_bits = len(encoded) * 8 - padding
    node = root
    for bit_index in range(total_bits):
        bit = (encoded[bit_index // 8] >> (7 - bit_index % 8)) & 1
        node = node.get(bit)
        if node is None:
            break
        if "leaf" in node:
            out.append(node["leaf"])
            node = root
    return bytes(out[:out_size])


def decode_block(header, block_data):
    encoding = block_data[0]
    payload = block_data[1:]
    width = header["width"]
    out_size = width * header["block_height"]  # 8-bit 索引流
    if encoding == 0:  # RLE
        return decode_rle(payload, out_size)
    if encoding == 1:  # Huffman + RLE
        tmp = decode_huffman(payload, out_size * 2)
        return decode_rle(tmp, out_size)
    if encoding == 3:  # Huffman 直接
        return decode_huffman(payload, out_size)
    if encoding == 5:  # RAW
        return payload[:out_size]
    raise ValueError("未知块编码: %d" % encoding)


def decode_frame(content, frame_base, table, index):
    fsz, foff = table[index]
    fd = content[frame_base + foff: frame_base + foff + fsz]
    h = parse_frame_header(fd)
    assert h["bit_depth"] == 8, "仅支持 8-bit 调色板帧, 当前 %d" % h["bit_depth"]
    f = h["file_data"]
    width, height = h["width"], h["height"]

    offsets = [h["data_offset"]]
    for k in range(1, h["blocks"]):
        offsets.append(offsets[-1] + h["block_lens"][k - 1])

    indices = bytearray(width * height)
    for block in range(h["blocks"]):
        bd = f[offsets[block]: offsets[block] + h["block_lens"][block]]
        decoded = decode_block(h, bd)
        start_row = block * h["block_height"]
        rows = min(h["block_height"], height - start_row)
        for r in range(rows):
            src = decoded[r * width:(r + 1) * width]
            dst = (start_row + r) * width
            indices[dst:dst + len(src)] = src

    # 调色板 RGBA(全不透明) -> RGB565 大端
    pal = h["palette"]
    n = h["num_colors"]
    rgba = bytearray(width * height * 4)
    for p in range(width * height):
        ci = indices[p]
        b, g, r, a = pal[ci * 4:ci * 4 + 4]
        o = p * 4
        rgba[o] = r
        rgba[o + 1] = g
        rgba[o + 2] = b
        rgba[o + 3] = a
    return bytes(rgba), width, height


# --------------------------------------------------------------------------
# 输出
# --------------------------------------------------------------------------
def rgb565_bigendian(rgba, w, h, out_size):
    img = Image.frombytes("RGBA", (w, h), rgba).resize((out_size, out_size), Image.LANCZOS)
    px = img.load()
    out = bytearray()
    for y in range(out_size):
        for x in range(out_size):
            r, g, b, a = px[x, y]
            r5 = r >> 3
            g6 = g >> 2
            b5 = b >> 3
            hi = (r5 << 3) | (g6 >> 3)
            lo = ((g6 & 0x07) << 5) | b5
            out.append(hi)
            out.append(lo)
    return bytes(out)


def c_byte_array(name, data):
    lines = ["static const uint8_t %s[] = {" % name]
    for i in range(0, len(data), BYTES_PER_LINE):
        chunk = data[i:i + BYTES_PER_LINE]
        lines.append("    " + ", ".join("0x%02x" % b for b in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def c_img_dsc(name, data_name, size):
    return (
        "static const lv_img_dsc_t %s = {\n"
        "    .header.cf = LV_IMG_CF_TRUE_COLOR,\n"
        "    .header.always_zero = 0,\n"
        "    .header.w = %d,\n"
        "    .header.h = %d,\n"
        "    .data_size = sizeof(%s),\n"
        "    .data = %s,\n"
        "};" % (name, size, size, data_name, data_name)
    )


def sample_indices(n, maxf):
    """从 0..n-1 均匀抽 maxf 帧, 首尾保留."""
    if maxf <= 1 or maxf >= n:
        return list(range(n))
    return [int(round(i * (n - 1) / (maxf - 1))) for i in range(maxf)]


def remap_loop(loop_start, loop_end, sampled):
    """把原始 loop 区间映射到抽样后的帧序号."""
    # sampled 单调递增且包含 loop_end(最后一个可用帧)
    ls = next((j for j, v in enumerate(sampled) if v >= loop_start), 0)
    le = len(sampled) - 1
    return ls, le


def generate(args):
    data = open(args.bin, "rb").read()
    files = parse_mmap(data)
    manifest = json.loads(files["index.json"].decode("utf-8"))

    wanted = set(args.expr) if args.expr else None
    exprs = []  # (name, frames_rgba_list, w, h, loop_start, loop_end, fps)

    for entry in manifest:
        name = entry["name"]
        if wanted is not None and name not in wanted:
            continue
        eaf = files[entry["file"]]
        total, table, frame_base = parse_eaf(eaf)
        fps = entry.get("fps", 25)

        loop = entry.get("loop", [])
        if len(loop) == 1:
            loop_start = loop_end = loop[0]
        elif len(loop) >= 2:
            loop_start, loop_end = loop[0], loop[1] - 1
        else:
            loop_start, loop_end = 0, total - 1
        loop_end = min(loop_end, total - 1)
        last_frame = loop_end  # 循环模式下 tail 帧(loop_end 之后)永不播放, 丢弃

        # 可用帧 0..last_frame
        usable = list(range(0, last_frame + 1))
        if args.max_frames and len(usable) > args.max_frames:
            sampled = sample_indices(len(usable), args.max_frames)
            usable = [usable[i] for i in sampled]
            loop_start, loop_end = remap_loop(loop_start, last_frame, usable)
        else:
            loop_start = loop_start  # 原始值即输出序号(全帧时)
            loop_end = last_frame

        frames = []
        w = h = 0
        for fi in usable:
            rgba, w, h = decode_frame(eaf, frame_base, table, fi)
            frames.append(rgba)
        exprs.append((name, frames, w, h, loop_start, loop_end, fps))

    if args.dump_png:
        os.makedirs(args.dump_png, exist_ok=True)
        for name, frames, w, h, _, _, _ in exprs:
            for i, rgba in enumerate(frames):
                Image.frombytes("RGBA", (w, h), rgba).save(
                    os.path.join(args.dump_png, "%s_%03d.png" % (name, i)))
        print("已导出 PNG 到 %s" % args.dump_png)

    if args.out is None:
        return  # 只 dump png

    os.makedirs(args.out, exist_ok=True)
    out_size = args.size
    per_frame = out_size * out_size * 2

    c_lines = ["/* 由 eaf_unpack.py 生成, 勿手动编辑 */", '#include "expression_frames.h"', ""]
    for name, frames, w, h, loop_start, loop_end, fps in exprs:
        dur = 1000 // fps
        data_chunks = []
        img_names = []
        for i, rgba in enumerate(frames):
            data_name = "%s_data_%d" % (name, i)
            img_name = "%s_frame_%d" % (name, i)
            data_chunks.append((data_name, rgb565_bigendian(rgba, w, h, out_size)))
            img_names.append(img_name)

        c_lines.append("/* ===== %s (%d 帧, loop %d..%d) ===== */" % (name, len(frames), loop_start, loop_end))
        for data_name, d in data_chunks:
            c_lines.append(c_byte_array(data_name, d))
            c_lines.append("")
        for i, img_name in enumerate(img_names):
            c_lines.append(c_img_dsc(img_name, "%s_data_%d" % (name, i), out_size))
        c_lines.append("")
        c_lines.append("const lv_img_dsc_t *%s_frames[] = {" % name)
        c_lines.append("    " + ", ".join("&%s" % n for n in img_names) + ",")
        c_lines.append("};")
        c_lines.append("")
        c_lines.append("const uint16_t %s_durations[] = {" % name)
        c_lines.append("    " + ", ".join(str(dur) for _ in img_names) + ",")
        c_lines.append("};")
        c_lines.append("")

    c_lines.append("/* ===== 注册表 ===== */")
    c_lines.append("const expression_desc_t expression_frames[] = {")
    for name, frames, w, h, loop_start, loop_end, fps in exprs:
        total_ms = len(frames) * (1000 // fps)
        c_lines.append('    { "%s", %s_frames, %s_durations, %d, %d, %d, %d },' % (
            name, name, name, len(frames), total_ms, loop_start, loop_end))
    c_lines.append("};")
    c_lines.append("")
    c_lines.append(
        "const int expression_frames_count = "
        "(int)(sizeof(expression_frames) / sizeof(expression_frames[0]));"
    )
    c_lines.append("")
    with open(os.path.join(args.out, "expression_frames.c"), "w", encoding="utf-8") as f:
        f.write("\n".join(c_lines))

    h_lines = ["/* 由 eaf_unpack.py 生成, 勿手动编辑 */", "#ifndef EXPRESSION_FRAMES_H",
               "#define EXPRESSION_FRAMES_H", "", "#include <lvgl.h>", "",
               "#ifdef __cplusplus", 'extern "C" {', "#endif", "",
               "typedef struct {",
               "    const char *name;",
               "    const lv_img_dsc_t **frames;",
               "    const uint16_t *durations;",
               "    uint16_t count;",
               "    uint32_t duration_ms;",
               "    uint16_t loop_start;",
               "    uint16_t loop_end;",
               "} expression_desc_t;", ""]
    for name, _, _, _, _, _, _ in exprs:
        h_lines.append("extern const lv_img_dsc_t *%s_frames[];" % name)
    h_lines += ["", "extern const expression_desc_t expression_frames[];",
                "extern const int expression_frames_count;", "", "#ifdef __cplusplus", "}",
                "#endif", "", "#endif", ""]
    with open(os.path.join(args.out, "expression_frames.h"), "w", encoding="utf-8") as f:
        f.write("\n".join(h_lines))

    print("生成完成:")
    total_kb = 0
    for name, frames, w, h, loop_start, loop_end, fps in exprs:
        kb = len(frames) * per_frame // 1024
        total_kb += kb
        print("  %-18s %3d 帧  %6d KB  (loop %d..%d)" % (name, len(frames), kb, loop_start, loop_end))
    print("合计 %d KB (%.2f MB)" % (total_kb, total_kb / 1024.0))


def main():
    ap = argparse.ArgumentParser(description="emote-assets.bin -> LVGL RGB565 帧表")
    ap.add_argument("--bin", default=DEFAULT_BIN, help="emote-assets.bin 路径")
    ap.add_argument("--out", default="lib/expressions", help="输出目录(不指定则只 dump png)")
    ap.add_argument("--size", type=int, default=360, help="缩放尺寸(正方形)")
    ap.add_argument("--expr", action="append", default=[], help="只处理指定表情(可重复, 默认全部)")
    ap.add_argument("--max-frames", type=int, default=0, help="每表情最大帧数(0=不抽帧)")
    ap.add_argument("--dump-png", default=None, help="导出 PNG 到该目录用于目检")
    args = ap.parse_args()
    generate(args)


if __name__ == "__main__":
    main()
