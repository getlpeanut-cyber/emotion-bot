#!/usr/bin/env python3
# -*- coding: gbk -*-
"""
从 asleep_[21.5s].gif 的"闭眼"过渡帧合成一个循环眨眼 GIF。

原理:
  asleep 动画中, 眼睛在帧 386(睁开) -> 457(完全闭合) 之间平滑闭合,
  期间头部/嘴巴位置稳定(已验证下半脸 bbox 几乎不变)。
  取闭合序列 + 镜像(睁开序列) 拼成 open->close->open 循环,
  并给"睁开"帧较长的时长, 形成自然的周期性眨眼(睁眼久、眨眼快)。
"""
from PIL import Image
import os

SRC = "d:/project_entire/project/emotion_bot/data/Emote Pack/gif/asleep_[21.5s].gif"
DST = "d:/project_entire/project/emotion_bot/data/Emote Pack/gif/blink_[0.6s].gif"

# 闭眼过渡的采样帧: (源帧索引, 显示时长 ms)
CLOSE = [(386, 300), (389, 40), (445, 40), (449, 40), (457, 40)]
# 镜像张开: 去掉首尾两个端点, 反向
OPEN = [(i, d) for i, d in reversed(CLOSE[1:-1])]
SEQ = CLOSE + OPEN  # open -> close -> open

def main():
    im = Image.open(SRC)
    canvas = Image.new("RGBA", im.size, (0, 0, 0, 0))
    full = []
    for i in range(im.n_frames):
        im.seek(i)
        canvas = Image.alpha_composite(canvas, im.convert("RGBA"))
        full.append(canvas.copy())

    frames = [full[i] for i, _ in SEQ]
    durs = [d for _, d in SEQ]

    # GIF 只支持 1bit 透明, 与源素材一致, 边缘质量不受影响
    frames[0].save(
        DST, save_all=True, append_images=frames[1:],
        duration=durs, loop=0, disposal=2, transparency=0,
    )

    # 回读校验
    chk = Image.open(DST)
    print("生成 %s: %d 帧, 时长 %s, 总 %d ms" % (DST, chk.n_frames, durs, sum(durs)))

if __name__ == "__main__":
    main()
