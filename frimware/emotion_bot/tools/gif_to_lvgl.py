#!/usr/bin/env python3
# -*- coding: gbk -*-
"""
将表情 GIF 转换为 LVGL RGB565 C 数组 + lv_img_dsc_t 帧表。

用法示例:
  python gif_to_lvgl.py --out lib/expressions --size 360 \
      --expr "smile_static=data/Emote Pack/gif/smile_static_[0.5s].gif" \
      --expr "smile=data/Emote Pack/gif/smile_[0.5s].gif"

生成:
  <out>/expression_frames.h   类型定义 + 帧表 extern 声明
  <out>/expression_frames.c   各表情帧数据 + 注册表 expression_frames[]

说明:
  - GIF 采用增量帧(disposal=1), 用 Image.alpha_composite 逐帧合成。
  - 透明背景被混合到 --bg 指定的背景色(默认黑色), 输出 RGB565。
  - LV_COLOR_16_SWAP=1 时 LVGL 内存布局为大端 RGB565, 即每个像素输出
    [R4..R0 G5G4G3][G2G1G0 B4..B0] 两个字节。
  - 不去重。--max-frames N 会从全帧中均匀抽取 N 帧(首尾保留)。
  - duration_ms 始终等于 GIF 全部帧时长之和(不随抽取而变), 配合 lv_animimg
    的均匀计时: 抽取帧数越少, 每帧停留时间越长(总时长不变)。
"""
import argparse
import os
from PIL import Image

BYTES_PER_LINE = 12


def extract_frames(path):
    """返回 (rgba_frames, durations_ms). 逐帧 alpha 合成."""
    im = Image.open(path)
    canvas = Image.new("RGBA", im.size, (0, 0, 0, 0))
    frames = []
    durations = []
    for i in range(im.n_frames):
        im.seek(i)
        frame = im.convert("RGBA")
        canvas = Image.alpha_composite(canvas, frame)
        frames.append(canvas.copy())
        durations.append(im.info.get("duration", 40) or 40)
    return frames, durations


def rgb565_bigendian(frame_rgba, bg, size):
    """缩放+混合背景+转大端 RGB565, 返回 bytes."""
    img = frame_rgba.resize((size, size), Image.LANCZOS)
    px = img.load()
    out = bytearray()
    br, bgc, bb = bg
    for y in range(size):
        for x in range(size):
            r, g, b, a = px[x, y]
            # 混合到背景色
            r = (r * a + br * (255 - a)) // 255
            g = (g * a + bgc * (255 - a)) // 255
            b = (b * a + bb * (255 - a)) // 255
            r5 = r >> 3
            g6 = g >> 2
            b5 = b >> 3
            hi = (r5 << 3) | (g6 >> 3)
            lo = ((g6 & 0x07) << 5) | b5
            out.append(hi)
            out.append(lo)
    return bytes(out)


def c_byte_array(name, data):
    lines = []
    lines.append("static const uint8_t %s[] = {" % name)
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


def generate(args):
    exprs = []  # list of (name, frames_data, count, duration_ms)
    for spec in args.expr:
        # 支持 name=path|N 指定单个表情最大帧数(| 不会出现在路径里)
        maxf = args.max_frames
        if "|" in spec:
            spec, _, tail = spec.rpartition("|")
            if tail.isdigit():
                maxf = int(tail)

        if "=" in spec:
            name, path = spec.split("=", 1)
        else:
            # 没有名字时用文件名(去扩展名)作为名字
            name = os.path.splitext(os.path.basename(spec))[0]
            path = spec
        name = name.strip()
        path = path.strip()
        if not name or not path:
            raise SystemExit("无效的 --expr: %r" % spec)

        frames, durations = extract_frames(path)
        total_ms = sum(durations)  # 总时长固定为 GIF 全帧之和, 不随抽取变化
        if maxf and len(frames) > maxf:
            if maxf == 1:
                frames = frames[:1]
                durations = [total_ms]
            else:
                idxs = [int(round(i * (len(frames) - 1) / (maxf - 1))) for i in range(maxf)]
                frames = [frames[i] for i in idxs]
                # 抽取后按帧均分总时长(保持总时长不变)
                base, rem = divmod(total_ms, maxf)
                durations = [base + (1 if i < rem else 0) for i in range(maxf)]

        data_chunks = []  # list of (data_name, bytes)
        img_names = []
        for i, f in enumerate(frames):
            data_name = "%s_data_%d" % (name, i)
            img_name = "%s_frame_%d" % (name, i)
            data = rgb565_bigendian(f, args.bg, args.size)
            data_chunks.append((data_name, data))
            img_names.append(img_name)
        exprs.append((name, data_chunks, img_names, durations, total_ms))

    os.makedirs(args.out, exist_ok=True)

    # ---- 生成 expression_frames.c ----
    c_lines = []
    c_lines.append('/* 由 gif_to_lvgl.py 生成, 勿手动编辑 */')
    c_lines.append('#include "expression_frames.h"')
    c_lines.append("")
    for name, data_chunks, img_names, durations, total_ms in exprs:
        c_lines.append("/* ===== %s (total %d ms) ===== */" % (name, total_ms))
        for data_name, data in data_chunks:
            c_lines.append(c_byte_array(data_name, data))
            c_lines.append("")
        for i, img_name in enumerate(img_names):
            data_name = "%s_data_%d" % (name, i)
            c_lines.append(c_img_dsc(img_name, data_name, args.size))
        c_lines.append("")
        c_lines.append(
            "const lv_img_dsc_t *%s_frames[] = {" % name
        )
        c_lines.append("    " + ", ".join("&%s" % n for n in img_names) + ",")
        c_lines.append("};")
        c_lines.append("")
        c_lines.append("const uint16_t %s_durations[] = {" % name)
        c_lines.append("    " + ", ".join(str(d) for d in durations) + ",")
        c_lines.append("};")
        c_lines.append("")

    c_lines.append("/* ===== 注册表 ===== */")
    c_lines.append("const expression_desc_t expression_frames[] = {")
    for name, data_chunks, img_names, durations, total_ms in exprs:
        c_lines.append(
            '    { "%s", %s_frames, %s_durations, %d, %d, 0, %d },' % (name, name, name, len(img_names), total_ms, len(img_names) - 1)
        )
    c_lines.append("};")
    c_lines.append("")
    c_lines.append(
        "const int expression_frames_count = "
        "(int)(sizeof(expression_frames) / sizeof(expression_frames[0]));"
    )
    c_lines.append("")
    with open(os.path.join(args.out, "expression_frames.c"), "w", encoding="utf-8") as f:
        f.write("\n".join(c_lines))

    # ---- 生成 expression_frames.h ----
    h_lines = []
    h_lines.append('/* 由 gif_to_lvgl.py 生成, 勿手动编辑 */')
    h_lines.append("#ifndef EXPRESSION_FRAMES_H")
    h_lines.append("#define EXPRESSION_FRAMES_H")
    h_lines.append("")
    h_lines.append("#include <lvgl.h>")
    h_lines.append("")
    h_lines.append("#ifdef __cplusplus")
    h_lines.append('extern "C" {')
    h_lines.append("#endif")
    h_lines.append("")
    h_lines.append("typedef struct {")
    h_lines.append("    const char *name;")
    h_lines.append("    const lv_img_dsc_t **frames;")
    h_lines.append("    const uint16_t *durations;")
    h_lines.append("    uint16_t count;")
    h_lines.append("    uint32_t duration_ms;")
    h_lines.append("    uint16_t loop_start;")
    h_lines.append("    uint16_t loop_end;")
    h_lines.append("} expression_desc_t;")
    h_lines.append("")
    for name, _, _, _, _ in exprs:
        h_lines.append("extern const lv_img_dsc_t *%s_frames[];" % name)
    h_lines.append("")
    h_lines.append("extern const expression_desc_t expression_frames[];")
    h_lines.append("extern const int expression_frames_count;")
    h_lines.append("")
    h_lines.append("#ifdef __cplusplus")
    h_lines.append("}")
    h_lines.append("#endif")
    h_lines.append("")
    h_lines.append("#endif")
    with open(os.path.join(args.out, "expression_frames.h"), "w", encoding="utf-8") as f:
        f.write("\n".join(h_lines))

    print("生成完成:")
    for name, data_chunks, img_names, durations, total_ms in exprs:
        total_bytes = sum(len(d) for _, d in data_chunks)
        print("  %-16s %3d 帧  %6d KB  (循环 %d ms)" % (
            name, len(img_names), total_bytes // 1024, total_ms))


def main():
    ap = argparse.ArgumentParser(description="GIF -> LVGL RGB565 帧表")
    ap.add_argument("--out", default="lib/expressions", help="输出目录")
    ap.add_argument("--size", type=int, default=360, help="缩放尺寸(正方形)")
    ap.add_argument("--bg", default="0,0,0", help="透明背景混合色 R,G,B")
    ap.add_argument("--max-frames", type=int, default=0, help="每表情最大帧数(0=全部)")
    ap.add_argument("--expr", action="append", default=[],
                    help="name=path 或 path")
    ap.add_argument("--list", dest="list_file", default=None,
                    help="从文件读取表情列表, 每行一个 name=path[:N]")
    args = ap.parse_args()
    if args.list_file:
        with open(args.list_file, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#"):
                    args.expr.append(line)
    if not args.expr:
        raise SystemExit("需要 --expr 或 --list 指定至少一个表情")
    args.bg = tuple(int(v) for v in args.bg.split(","))
    generate(args)


if __name__ == "__main__":
    main()
