#!/usr/bin/env python3
# 生成 nv12中yuv分布效果图和读取方式.md 用的 SVG 图
# 用法（在本目录下）：python3 gen_nv12_svg.py .
import os, sys

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)

FONT = "-apple-system, 'PingFang SC', 'Microsoft YaHei', 'Noto Sans CJK SC', Helvetica, Arial, sans-serif"
MONO = "Menlo, Consolas, 'DejaVu Sans Mono', monospace"

# 分量颜色（文字/描边）
C_Y, C_U, C_V, C_PAD = "#2F5D9A", "#2E7D32", "#C25A12", "#8A8A8A"
F_Y, F_U, F_V = "#DCE9F7", "#D8F0D2", "#FBE1CF"
# 2x2 分组底色（同一组的 4 个 Y 和它们共用的 U、V 用同一种底色）
G = ["#DCE9F7", "#E9DFF7", "#D6F0EC", "#FCEFD0", "#F9DDE6", "#E4EDD3"]
G_EDGE = ["#3E6FB0", "#7A55B3", "#2A8C80", "#B8860B", "#B5476E", "#6B8E23"]
RED = "#D32F2F"


class Svg:
    def __init__(self, w, h, title):
        self.w, self.h = w, h
        self.items = []
        self.title = title

    def add(self, s):
        self.items.append(s)

    def rect(self, x, y, w, h, fill="none", stroke="#555", sw=1, rx=0, dash=None, extra=""):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d} {extra}/>')

    def text(self, x, y, s, size=13, color="#222", anchor="start", weight="normal", mono=False, extra=""):
        fam = MONO if mono else FONT
        s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        self.add(f'<text x="{x}" y="{y}" font-family="{fam}" font-size="{size}" fill="{color}" text-anchor="{anchor}" font-weight="{weight}" dominant-baseline="middle" {extra}>{s}</text>')

    def line(self, x1, y1, x2, y2, color="#555", sw=1.2, arrow=False, dash=None):
        a = ' marker-end="url(#arrow)"' if arrow else ""
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.add(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" stroke-width="{sw}"{a}{d}/>')

    def path(self, d, color="#555", sw=1.2, arrow=False, dash=None, fill="none"):
        a = ' marker-end="url(#arrow)"' if arrow else ""
        da = f' stroke-dasharray="{dash}"' if dash else ""
        self.add(f'<path d="{d}" stroke="{color}" stroke-width="{sw}" fill="{fill}"{a}{da}/>')

    def bracket_h(self, x1, x2, y, label, color="#444", up=True, size=12):
        t = -6 if up else 6
        self.path(f"M{x1},{y+t} L{x1},{y} L{x2},{y} L{x2},{y+t}", color=color)
        self.text((x1 + x2) / 2, y - 10 if up else y + 14, label, size=size, color=color, anchor="middle")

    def bracket_v(self, x, y1, y2, label, color="#444", right=True, size=12, anchor=None):
        t = -6 if right else 6
        self.path(f"M{x+t},{y1} L{x},{y1} L{x},{y2} L{x+t},{y2}", color=color)
        tx = x + 8 if right else x - 8
        self.text(tx, (y1 + y2) / 2, label, size=size, color=color, anchor=anchor or ("start" if right else "end"))

    def save(self, name):
        defs = f'''<defs>
  <pattern id="hatch" width="6" height="6" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">
    <rect width="6" height="6" fill="#F1F1F1"/><line x1="0" y1="0" x2="0" y2="6" stroke="#C4C4C4" stroke-width="2.2"/>
  </pattern>
  <marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
    <path d="M0,0 L10,5 L0,10 z" fill="context-stroke"/>
  </marker>
</defs>'''
        body = "\n".join(self.items)
        svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" viewBox="0 0 {self.w} {self.h}">
<title>{self.title}</title>
{defs}
<rect x="0" y="0" width="{self.w}" height="{self.h}" rx="10" fill="#FFFFFF" stroke="#DDDDDD"/>
{body}
</svg>
'''
        with open(os.path.join(OUT, name), "w", encoding="utf-8") as f:
            f.write(svg)


def cell(s, x, y, w, h, label, fill, color, bold=False, size=12, edge="#666", sw=1):
    s.rect(x, y, w, h, fill=fill, stroke=edge, sw=sw)
    if label:
        s.text(x + w / 2, y + h / 2 + 1, label, size=size, color=color, anchor="middle", mono=True,
               weight="bold" if bold else "normal")


def pad(s, x, y, w, h):
    s.rect(x, y, w, h, fill="url(#hatch)", stroke="#B0B0B0")


def legend(s, x, y, items):
    for i, (fill, edge, label) in enumerate(items):
        xx = x + i * 150
        if fill == "hatch":
            s.rect(xx, y - 8, 16, 16, fill="url(#hatch)", stroke="#B0B0B0")
        else:
            s.rect(xx, y - 8, 16, 16, fill=fill, stroke=edge)
        s.text(xx + 22, y, label, size=12, color="#333")


def grp(r, c, cols):
    """像素 (r, c) 属于第几个 2x2 组"""
    return (r // 2) * (cols // 2) + (c // 2)


# =====================================================================
# 图 1：YUV 4:2:0 采样（4x4 像素）
# =====================================================================
def fig1():
    s = Svg(880, 430, "YUV 4:2:0 采样")
    s.text(30, 30, "YUV 4:2:0：每个像素一个 Y，每 2×2 = 4 个像素共用一个 U 和一个 V", size=16, weight="bold")
    # 左：4x4 像素
    X0, Y0, CW, CH = 50, 90, 64, 52
    s.text(X0 + 2 * CW, Y0 - 24, "一张 4×4 的图（16 个像素）", size=13, color="#444", anchor="middle")
    for r in range(4):
        for c in range(4):
            g = grp(r, c, 4)
            cell(s, X0 + c * CW, Y0 + r * CH, CW, CH, f"Y{r}{c}", G[g], C_Y, size=13)
    for gr in range(2):
        for gc in range(2):
            g = gr * 2 + gc
            s.rect(X0 + gc * 2 * CW, Y0 + gr * 2 * CH, 2 * CW, 2 * CH, stroke=G_EDGE[g], sw=3)
    s.text(X0 + 2 * CW, Y0 + 4 * CH + 24, "同一种底色的 4 个像素 = 一组，共用一对 U、V", size=12, color="#555", anchor="middle")
    # 中间箭头
    s.line(X0 + 4 * CW + 20, Y0 + 2 * CH, X0 + 4 * CW + 80, Y0 + 2 * CH, color="#777", sw=2, arrow=True)
    # 右：存储的数据
    RX = X0 + 4 * CW + 110
    s.text(RX, Y0 - 24, "要存的数据", size=13, color="#444")
    # Y 16 个
    s.text(RX, Y0 + 14, "Y：16 个", size=13, color=C_Y, weight="bold")
    yw = 22
    for i in range(16):
        r, c = divmod(i, 4)
        cell(s, RX + 80 + i * yw, Y0, yw, 28, "", G[grp(r, c, 4)], C_Y)
    s.text(RX + 80 + 8 * yw, Y0 + 44, "每个像素 1 个，共 4 × 4 = 16 字节", size=11, color="#666", anchor="middle")
    # U 4 个
    UY = Y0 + 90
    s.text(RX, UY + 14, "U：4 个", size=13, color=C_U, weight="bold")
    for g in range(4):
        cell(s, RX + 80 + g * 70, UY, 64, 28, f"U{g//2}{g%2}", G[g], C_U, size=12, edge=G_EDGE[g], sw=2)
    # V 4 个
    VY = UY + 60
    s.text(RX, VY + 14, "V：4 个", size=13, color=C_V, weight="bold")
    for g in range(4):
        cell(s, RX + 80 + g * 70, VY, 64, 28, f"V{g//2}{g%2}", G[g], C_V, size=12, edge=G_EDGE[g], sw=2)
    s.text(RX + 80 + 140, VY + 50, "每组 1 个 U、1 个 V，共 (4/2) × (4/2) = 4 组", size=11, color="#666", anchor="middle")
    # 底部算式
    s.rect(30, 340, 820, 64, fill="#F7F7F7", stroke="#E0E0E0", rx=6)
    s.text(50, 360, "合计：16 (Y) + 4 (U) + 4 (V) = 24 字节 = 16 × 3/2", size=14, weight="bold")
    s.text(50, 386, "1920×1080：Y = 2,073,600，U = V = 518,400，一帧共 1920 × 1080 × 3/2 = 3,110,400 字节（代码里 × 3 / 2 的来历）", size=12, color="#444")
    s.save("01_yuv420_sampling.svg")


# =====================================================================
# 图 2：6x4 的 NV12 文件布局
# =====================================================================
def fig2():
    W, H = 6, 4
    s = Svg(940, 560, "NV12 文件布局（6×4）")
    s.text(30, 30, "文件里的 NV12（6×4，一共 6 × 4 × 3/2 = 36 字节，紧密排列、没有填充）", size=16, weight="bold")
    X0, Y0, CW, CH = 150, 90, 62, 40
    s.bracket_h(X0, X0 + W * CW, Y0 - 12, "width = 6 字节")
    # Y 平面
    for r in range(H):
        s.text(X0 - 14, Y0 + r * CH + CH / 2, f"Y 第 {r} 行", size=12, color=C_Y, anchor="end")
        for c in range(W):
            cell(s, X0 + c * CW, Y0 + r * CH, CW, CH, f"Y{r}{c}", G[grp(r, c, W)], C_Y, size=13)
        s.text(X0 + W * CW + 16, Y0 + r * CH + CH / 2, f"偏移 {r * W}", size=12, color="#555", mono=True)
    s.bracket_v(X0 + W * CW + 110, Y0, Y0 + H * CH, "Y 平面：height = 4 行", color=C_Y)
    # UV 平面
    UY0 = Y0 + H * CH + 14
    for r in range(H // 2):
        s.text(X0 - 14, UY0 + r * CH + CH / 2, f"UV 第 {r} 行", size=12, color=C_U, anchor="end")
        for k in range(W // 2):
            g = r * (W // 2) + k
            cell(s, X0 + (2 * k) * CW, UY0 + r * CH, CW, CH, f"U{r}{k}", G[g], C_U, size=13, edge=G_EDGE[g], sw=1.5)
            cell(s, X0 + (2 * k + 1) * CW, UY0 + r * CH, CW, CH, f"V{r}{k}", G[g], C_V, size=13, edge=G_EDGE[g], sw=1.5)
        s.text(X0 + W * CW + 16, UY0 + r * CH + CH / 2, f"偏移 {W * H + r * W}", size=12, color="#555", mono=True)
    s.bracket_v(X0 + W * CW + 110, UY0, UY0 + 2 * CH, "UV 平面：height/2 = 2 行", color=C_U)
    s.text(X0 + W * CW + 118, UY0 + 2 * CH + 18, "每行还是 width = 6 字节（UVUVUV）", size=11, color="#666")
    # 示例：U01 V01 管 Y02 Y03 Y12 Y13
    s.rect(X0 + 2 * CW, Y0, 2 * CW, 2 * CH, stroke=RED, sw=2.5, dash="6,3")
    s.rect(X0 + 2 * CW, UY0, 2 * CW, CH, stroke=RED, sw=2.5, dash="6,3")
    s.text(X0 + 3 * CW, UY0 + 2 * CH + 52, "例：U01、V01 这一对，管的是 Y02 Y03 Y12 Y13 这 4 个像素（同一种底色）", size=12, color=RED, anchor="middle")
    # 底部：一维字节流
    BY = 470
    s.text(30, BY - 22, "在文件里其实就是一长串字节，一行接一行（数字 = 文件偏移）：", size=12, color="#444")
    bw = 24
    for i in range(36):
        if i < 24:
            r, c = divmod(i, W)
            fill, col, lab = G[grp(r, c, W)], C_Y, "Y"
        else:
            j = i - 24
            r, c = divmod(j, W)
            g = r * (W // 2) + c // 2
            fill = G[g]
            col, lab = (C_U, "U") if c % 2 == 0 else (C_V, "V")
        cell(s, 30 + i * bw, BY, bw, 28, lab, fill, col, size=11)
        if i % 6 == 0:
            s.text(30 + i * bw + 2, BY + 42, str(i), size=10, color="#555", mono=True)
    s.text(30 + 36 * bw + 2, BY + 42, "36", size=10, color="#555", mono=True)
    s.bracket_h(30, 30 + 24 * bw, BY + 58, "Y：24 字节", up=False, color=C_Y, size=11)
    s.bracket_h(30 + 24 * bw, 30 + 36 * bw, BY + 58, "UV：12 字节", up=False, color=C_U, size=11)
    s.h = 560
    s.save("02_file_6x4.svg")


# =====================================================================
# 图 3：1920x1080 的 NV12 文件
# =====================================================================
def fig3():
    s = Svg(900, 600, "NV12 文件布局（1920×1080）")
    s.text(30, 30, "1920×1080 的 NV12 文件：一帧 3,110,400 字节，一帧紧挨着一帧", size=16, weight="bold")
    X0, Y0, BW = 200, 80, 300
    sc = 0.25
    yh, uh = 1080 * sc, 540 * sc
    s.bracket_h(X0, X0 + BW, Y0 - 10, "width = 1920 字节 / 行")
    s.rect(X0, Y0, BW, yh, fill=F_Y, stroke=C_Y, sw=1.5)
    for i in range(1, 10):
        s.line(X0, Y0 + i * yh / 10, X0 + BW, Y0 + i * yh / 10, color="#B9CDE6", sw=0.8)
    s.text(X0 + BW / 2, Y0 + yh / 2 - 12, "Y 平面", size=16, color=C_Y, anchor="middle", weight="bold")
    s.text(X0 + BW / 2, Y0 + yh / 2 + 12, "第 0 ～ 1079 行", size=13, color=C_Y, anchor="middle")
    s.rect(X0, Y0 + yh, BW, uh, fill=F_U, stroke=C_U, sw=1.5)
    for i in range(1, 6):
        s.line(X0, Y0 + yh + i * uh / 6, X0 + BW, Y0 + yh + i * uh / 6, color="#B7DDB0", sw=0.8)
    s.text(X0 + BW / 2, Y0 + yh + uh / 2 - 12, "UV 平面（UVUVUV…）", size=16, color=C_U, anchor="middle", weight="bold")
    s.text(X0 + BW / 2, Y0 + yh + uh / 2 + 12, "第 0 ～ 539 行", size=13, color=C_U, anchor="middle")
    # 偏移
    for yy, lab in [(Y0, "偏移 0"), (Y0 + yh, "偏移 2,073,600"), (Y0 + yh + uh, "偏移 3,110,400")]:
        s.line(X0 - 10, yy, X0, yy, color="#555")
        s.text(X0 - 14, yy, lab, size=12, color="#333", anchor="end", mono=True)
    s.text(X0 - 14, Y0 + yh + 16, "= 1920 × 1080", size=11, color="#777", anchor="end", mono=True)
    s.text(X0 - 14, Y0 + yh + uh + 16, "下一帧从这里开始", size=11, color=RED, anchor="end")
    s.bracket_v(X0 + BW + 14, Y0, Y0 + yh, "1080 行 × 1920 = 2,073,600 字节", color=C_Y)
    s.bracket_v(X0 + BW + 14, Y0 + yh, Y0 + yh + uh, "540 行 × 1920 = 1,036,800 字节", color=C_U)
    # 帧序列
    FY = 545
    s.text(30, FY - 26, "整个文件（60 帧）：", size=12, color="#444")
    fw = 13.5
    for i in range(60):
        x = 30 + i * fw
        s.rect(x, FY - 10, fw * 2 / 3, 20, fill=F_Y, stroke=C_Y, sw=0.6)
        s.rect(x + fw * 2 / 3, FY - 10, fw / 3, 20, fill=F_U, stroke=C_U, sw=0.6)
    s.text(30, FY + 22, "0", size=10, color="#555", mono=True)
    s.text(30 + 60 * fw, FY + 22, "186,624,000", size=10, color="#555", mono=True, anchor="end")
    s.text(30 + 30 * fw, FY + 22, "每一小格 = 一帧 3,110,400 字节（蓝 = Y，绿 = UV），60 帧首尾相接，中间没有任何填充", size=10, color="#777", anchor="middle")
    s.save("03_file_1080p.svg")


# =====================================================================
# 图 4：6x4 在 MPP 硬件缓冲区里（hor_stride = 8, ver_stride = 6）
# =====================================================================
def fig4():
    W, H, HS, VS = 6, 4, 8, 6
    s = Svg(1060, 640, "NV12 硬件缓冲区布局（6×4，stride 8×6）")
    s.text(30, 30, "硬件缓冲区 frmBuf 里的 NV12（6×4，假设 hor_stride = 8、ver_stride = 6；▨ = 填充）", size=16, weight="bold")
    X0, Y0, CW, CH = 270, 110, 56, 36
    s.bracket_h(X0, X0 + W * CW, Y0 - 12, "width = 6", color="#333")
    s.bracket_h(X0, X0 + HS * CW, Y0 - 40, "hor_stride = 8 字节（每一行占的字节数）", color="#B23")
    for r in range(VS):
        y = Y0 + r * CH
        lab = f"Y 第 {r} 行" if r < H else f"第 {r} 行（填充）"
        s.text(X0 - 14, y + CH / 2, lab, size=12, color=C_Y if r < H else C_PAD, anchor="end")
        for c in range(HS):
            if r < H and c < W:
                cell(s, X0 + c * CW, y, CW, CH, f"Y{r}{c}", G[grp(r, c, W)], C_Y, size=12)
            else:
                pad(s, X0 + c * CW, y, CW, CH)
        s.text(X0 + HS * CW + 14, y + CH / 2, f"偏移 {r * HS}", size=12, color="#555", mono=True)
    s.bracket_v(X0 + HS * CW + 100, Y0, Y0 + H * CH, "height = 4 行有效", color=C_Y)
    s.bracket_v(X0 + HS * CW + 100, Y0 + H * CH, Y0 + VS * CH, "ver_stride − height = 2 行填充", color=C_PAD)
    s.bracket_v(X0 - 125, Y0, Y0 + VS * CH, "ver_stride = 6 行", color="#B23", right=False)
    # UV
    UY0 = Y0 + VS * CH + 16
    for r in range(VS // 2):
        y = UY0 + r * CH
        lab = f"UV 第 {r} 行" if r < H // 2 else f"UV 第 {r} 行（填充）"
        s.text(X0 - 14, y + CH / 2, lab, size=12, color=C_U if r < H // 2 else C_PAD, anchor="end")
        for c in range(HS):
            if r < H // 2 and c < W:
                k = c // 2
                g = r * (W // 2) + k
                if c % 2 == 0:
                    cell(s, X0 + c * CW, y, CW, CH, f"U{r}{k}", G[g], C_U, size=12, edge=G_EDGE[g], sw=1.5)
                else:
                    cell(s, X0 + c * CW, y, CW, CH, f"V{r}{k}", G[g], C_V, size=12, edge=G_EDGE[g], sw=1.5)
            else:
                pad(s, X0 + c * CW, y, CW, CH)
        s.text(X0 + HS * CW + 14, y + CH / 2, f"偏移 {HS * VS + r * HS}", size=12, color="#555", mono=True)
    s.bracket_v(X0 + HS * CW + 100, UY0, UY0 + 2 * CH, "height/2 = 2 行有效", color=C_U)
    s.bracket_v(X0 + HS * CW + 100, UY0 + 2 * CH, UY0 + 3 * CH, "填充（UV 也有 ver_stride/2 = 3 行）", color=C_PAD)
    # UV 起点标注
    s.line(X0 - 6, UY0, X0 + HS * CW + 6, UY0, color=RED, sw=2, dash="6,3")
    s.text(X0 + HS * CW / 2, UY0 + 3 * CH + 28, "UV 起点 = hor_stride × ver_stride = 8 × 6 = 48（不是 width × height = 24）", size=13, color=RED, anchor="middle", weight="bold")
    # 底部说明
    BY = UY0 + 3 * CH + 64
    s.rect(30, BY, 1000, 76, fill="#F7F7F7", stroke="#E0E0E0", rx=6)
    s.text(48, BY + 22, "和文件相比有两处不一样：", size=13, weight="bold")
    s.text(48, BY + 44, "① 每行末尾多了 hor_stride − width 个填充字节 → 第 row 行的起点是 row × hor_stride，不是 row × width", size=12, color="#333")
    s.text(48, BY + 64, "② Y 平面多了 ver_stride − height 行填充 → UV 平面的起点是 hor_stride × ver_stride，不是 width × height", size=12, color="#333")
    s.h = BY + 96
    s.save("04_buffer_6x4.svg")


# =====================================================================
# 图 5：1920x1080 在 MPP 硬件缓冲区里
# =====================================================================
def fig5():
    s = Svg(980, 600, "NV12 硬件缓冲区布局（1920×1080）")
    s.text(30, 30, "1920×1080 在硬件缓冲区里：hor_stride = 1920，ver_stride = 1088（填充行为了看清楚放大画了）", size=16, weight="bold")
    X0, Y0, BW = 300, 80, 300
    sc = 0.30
    yh, ph, uh, uph = 1080 * sc, 26, 540 * sc, 16   # 填充放大
    s.bracket_h(X0, X0 + BW, Y0 - 10, "hor_stride = width = 1920（1920 是 16 的倍数，行尾没有填充）")
    y = Y0
    s.rect(X0, y, BW, yh, fill=F_Y, stroke=C_Y, sw=1.5)
    s.text(X0 + BW / 2, y + yh / 2 - 12, "Y 平面", size=16, color=C_Y, anchor="middle", weight="bold")
    s.text(X0 + BW / 2, y + yh / 2 + 12, "第 0 ～ 1079 行（从文件读进来）", size=12, color=C_Y, anchor="middle")
    y += yh
    s.add(f'<rect x="{X0}" y="{y}" width="{BW}" height="{ph}" fill="url(#hatch)" stroke="#999"/>')
    s.text(X0 + BW / 2, y + ph / 2, "第 1080 ～ 1087 行：填充（不读不写）", size=12, color="#555", anchor="middle")
    y += ph
    s.rect(X0, y, BW, uh, fill=F_U, stroke=C_U, sw=1.5)
    s.text(X0 + BW / 2, y + uh / 2 - 12, "UV 平面（UVUVUV…）", size=16, color=C_U, anchor="middle", weight="bold")
    s.text(X0 + BW / 2, y + uh / 2 + 12, "UV 第 0 ～ 539 行（从文件读进来）", size=12, color=C_U, anchor="middle")
    y += uh
    s.add(f'<rect x="{X0}" y="{y}" width="{BW}" height="{uph}" fill="url(#hatch)" stroke="#999"/>')
    s.text(X0 + BW / 2, y + uph / 2, "UV 填充 4 行", size=11, color="#555", anchor="middle")
    y += uph
    # 偏移
    marks = [
        (Y0, "偏移 0", "#333"),
        (Y0 + yh, "偏移 2,073,600", "#333"),
        (Y0 + yh + ph, "偏移 2,088,960", RED),
        (Y0 + yh + ph + uh, "偏移 3,125,760", "#333"),
        (y, "偏移 3,133,440", "#333"),
    ]
    for yy, lab, col in marks:
        s.line(X0 - 10, yy, X0, yy, color=col)
        s.text(X0 - 14, yy, lab, size=12, color=col, anchor="end", mono=True, weight="bold" if col == RED else "normal")
    s.text(X0 - 14, Y0 + yh + ph + 16, "= hor_stride × ver_stride（UV 起点）", size=11, color=RED, anchor="end")
    s.text(X0 - 14, y + 16, "= frameSize（1920 × 1088 × 3/2）", size=11, color="#777", anchor="end")
    s.bracket_v(X0 + BW + 14, Y0, Y0 + yh, "height = 1080 行有效", color=C_Y)
    s.bracket_v(X0 + BW + 14, Y0 + yh, Y0 + yh + ph, "8 行填充 = 15,360 字节", color=C_PAD)
    s.bracket_v(X0 + BW + 150, Y0, Y0 + yh + ph, "ver_stride = 1088 行", color="#B23")
    s.bracket_v(X0 + BW + 14, Y0 + yh + ph, Y0 + yh + ph + uh, "540 行有效", color=C_U)
    s.bracket_v(X0 + BW + 14, Y0 + yh + ph + uh, y, "4 行填充（1088/2 − 1080/2）", color=C_PAD)
    # 底部
    BY = y + 40
    s.rect(30, BY, 920, 56, fill="#FFF4F4", stroke="#F2C9C9", rx=6)
    s.text(48, BY + 18, "最关键的一个数：UV 在文件里从 2,073,600 开始，在缓冲区里从 2,088,960 开始，差 15,360 字节（8 行）", size=13, color=RED, weight="bold")
    s.text(48, BY + 40, "所以不能把文件整块 fread 进来，必须按 stride 逐行搬（见下一张图）", size=12, color="#333")
    s.h = BY + 76
    s.save("05_buffer_1080p.svg")


# =====================================================================
# 图 6：逐行搬运 vs 整帧 fread（6x4，stride 8x6）
# =====================================================================
def fig6():
    W, H, HS, VS = 6, 4, 8, 6
    s = Svg(1180, 980, "read_nv12_frame 逐行搬运")
    s.text(30, 30, "read_nv12_frame：从文件逐行读，写到缓冲区里对的位置（6×4，hor_stride = 8，ver_stride = 6）", size=16, weight="bold")
    CW, CH = 40, 32
    FX, FY = 60, 110          # 文件
    BX, BY = 640, 110         # 缓冲区
    s.text(FX, FY - 30, "文件（紧密排列，每行 6 字节）", size=14, weight="bold", color="#333")
    s.text(BX, FY - 30, "硬件缓冲区 frmBuf（每行 8 字节，▨ = 填充）", size=14, weight="bold", color="#333")
    # 文件：6 行（4 行 Y + 2 行 UV），每行 6 字节
    frows = []
    for r in range(6):
        y = FY + r * (CH + 10)
        frows.append(y)
        for c in range(W):
            if r < H:
                cell(s, FX + c * CW, y, CW, CH, f"Y{r}{c}", G[grp(r, c, W)], C_Y, size=11)
            else:
                rr = r - H
                k = c // 2
                g = rr * (W // 2) + k
                if c % 2 == 0:
                    cell(s, FX + c * CW, y, CW, CH, f"U{rr}{k}", G[g], C_U, size=11)
                else:
                    cell(s, FX + c * CW, y, CW, CH, f"V{rr}{k}", G[g], C_V, size=11)
        s.text(FX + W * CW + 10, y + CH / 2, f"偏移 {r * W}", size=11, color="#555", mono=True)
    # 缓冲区：9 行（6 行 Y 区 + 3 行 UV 区）
    brows = []
    for r in range(VS + VS // 2):
        y = BY + r * (CH + 10) + (12 if r >= VS else 0)
        brows.append(y)
        for c in range(HS):
            if r < H and c < W:
                cell(s, BX + c * CW, y, CW, CH, f"Y{r}{c}", G[grp(r, c, W)], C_Y, size=11)
            elif VS <= r < VS + H // 2 and c < W:
                rr = r - VS
                k = c // 2
                g = rr * (W // 2) + k
                if c % 2 == 0:
                    cell(s, BX + c * CW, y, CW, CH, f"U{rr}{k}", G[g], C_U, size=11)
                else:
                    cell(s, BX + c * CW, y, CW, CH, f"V{rr}{k}", G[g], C_V, size=11)
            else:
                pad(s, BX + c * CW, y, CW, CH)
        s.text(BX + HS * CW + 10, y + CH / 2, f"偏移 {r * HS}", size=11, color="#555", mono=True)
    # 箭头：文件行 → 缓冲区行
    mapping = [(0, 0), (1, 1), (2, 2), (3, 3), (4, 6), (5, 7)]
    for fr, br in mapping:
        y1 = frows[fr] + CH / 2
        y2 = brows[br] + CH / 2
        col = C_Y if fr < H else C_U
        s.path(f"M{FX + W * CW + 70},{y1} C{FX + W * CW + 200},{y1} {BX - 130},{y2} {BX - 8},{y2}", color=col, sw=1.6, arrow=True)
    MX = (FX + W * CW + 70 + BX) / 2
    s.text(MX, FY - 38, "① Y：for row < height（4 行）", size=12, color=C_Y, anchor="middle", weight="bold")
    s.text(MX, FY - 20, "fread(dst + row × hor_stride, 1, width)", size=11, color=C_Y, anchor="middle", mono=True)
    s.text(MX, brows[8] + 4, "② UV：for row < height/2（2 行）", size=12, color=C_U, anchor="middle", weight="bold")
    s.text(MX, brows[8] + 22, "fread(dst_uv + row × hor_stride, 1, width)", size=11, color=C_U, anchor="middle", mono=True)
    s.line(BX - 6, brows[VS] - 6, BX + HS * CW + 6, brows[VS] - 6, color=RED, sw=2, dash="6,3")
    s.text(BX, brows[VS] - 18, "dst_uv = dst + hor_stride × ver_stride = 48", size=11, color=RED, mono=True)
    s.text(FX, frows[5] + CH + 30, "文件位置自动往后走：读完 Y 第 3 行正好到 UV 开头（偏移 24），", size=11, color="#666")
    s.text(FX, frows[5] + CH + 48, "读完 UV 第 1 行正好到下一帧开头（偏移 36），所以不需要 fseek", size=11, color="#666")
    # 下半部分：整帧 fread 的错误结果
    EY = BY + 9 * (CH + 10) + 80
    s.line(30, EY - 30, 1150, EY - 30, color="#DDD")
    s.text(30, EY - 6, "❌ 如果整帧一次 fread(dst, 1, 36)：36 个字节从缓冲区偏移 0 开始一口气写下去", size=14, color=RED, weight="bold")
    CW2, CH2 = 40, 30
    EX = 160
    for r in range(VS + VS // 2):
        y = EY + 20 + r * (CH2 + 6) + (10 if r >= VS else 0)
        s.text(EX - 14, y + CH2 / 2, f"偏移 {r * HS}", size=11, color="#555", anchor="end", mono=True)
        for c in range(HS):
            i = r * HS + c
            if i < 36:
                if i < 24:
                    rr, cc = divmod(i, W)
                    cell(s, EX + c * CW2, y, CW2, CH2, f"Y{rr}{cc}", G[grp(rr, cc, W)], C_Y, size=10)
                else:
                    j = i - 24
                    rr, cc = divmod(j, W)
                    k = cc // 2
                    g = rr * (W // 2) + k
                    lab = f"U{rr}{k}" if cc % 2 == 0 else f"V{rr}{k}"
                    cell(s, EX + c * CW2, y, CW2, CH2, lab, G[g], C_U if cc % 2 == 0 else C_V, size=10)
            else:
                cell(s, EX + c * CW2, y, CW2, CH2, "?", "#FFFFFF", "#BBB", size=10, edge="#CCC")
    s.line(EX - 4, EY + 20 + VS * (CH2 + 6) + 4, EX + HS * CW2 + 4, EY + 20 + VS * (CH2 + 6) + 4, color=RED, sw=2, dash="6,3")
    tx = EX + HS * CW2 + 40
    s.text(tx, EY + 40, "问题 ①：每行塞进了 8 个字节，下一行的像素跑到了上一行行尾", size=12, color="#333")
    s.text(tx, EY + 60, "→ Y10、Y11 出现在第 0 行，画面每一行都往左错，看起来是斜着错位", size=12, color="#333")
    s.text(tx, EY + 92, "问题 ②：UV 的数据落在偏移 24（第 3 行），", size=12, color="#333")
    s.text(tx, EY + 112, "但编码器去偏移 48（红线）找 UV，那里是空的（?）", size=12, color="#333")
    s.text(tx, EY + 132, "→ 颜色全错，画面发绿", size=12, color="#333")
    s.text(tx, EY + 168, "1920×1080 时 hor_stride = width，问题 ① 不出现，", size=12, color="#666")
    s.text(tx, EY + 188, "但问题 ② 照样有：UV 错位 8 行 → 底部发绿", size=12, color="#666")
    s.h = EY + 20 + 9 * (CH2 + 6) + 40
    s.save("06_fread_row_by_row.svg")


# =====================================================================
# 图 7：找任意像素的 Y、U、V（6x4，stride 8x6，像素 (3,1)）
# =====================================================================
def fig7():
    W, H, HS, VS = 6, 4, 8, 6
    px, py = 3, 1
    s = Svg(1000, 560, "定位像素 (3,1) 的 Y、U、V")
    s.text(30, 30, "找任意一个像素的 Y、U、V：以像素 (x = 3, y = 1) 为例（6×4，hor_stride = 8，ver_stride = 6）", size=16, weight="bold")
    X0, Y0, CW, CH = 130, 80, 50, 32
    yoff = py * HS + px
    uoff = HS * VS + (py // 2) * HS + (px // 2) * 2
    for r in range(VS + VS // 2):
        y = Y0 + r * (CH + 4) + (12 if r >= VS else 0)
        s.text(X0 - 12, y + CH / 2, f"{r * HS}", size=11, color="#555", anchor="end", mono=True)
        for c in range(HS):
            off = r * HS + c
            hl = off in (yoff, uoff, uoff + 1)
            if r < H and c < W:
                cell(s, X0 + c * CW, y, CW, CH, f"Y{r}{c}", "#FFE08A" if hl else F_Y, C_Y, size=11, bold=hl,
                     edge=RED if hl else "#666", sw=2.5 if hl else 1)
            elif VS <= r < VS + H // 2 and c < W:
                rr, k = r - VS, c // 2
                lab = f"U{rr}{k}" if c % 2 == 0 else f"V{rr}{k}"
                col = C_U if c % 2 == 0 else C_V
                f = (F_U if c % 2 == 0 else F_V)
                cell(s, X0 + c * CW, y, CW, CH, lab, "#FFE08A" if hl else f, col, size=11, bold=hl,
                     edge=RED if hl else "#666", sw=2.5 if hl else 1)
            else:
                pad(s, X0 + c * CW, y, CW, CH)
    s.text(X0 - 12, Y0 - 16, "偏移", size=11, color="#555", anchor="end")
    for c in range(HS):
        s.text(X0 + c * CW + CW / 2, Y0 - 16, f"+{c}", size=10, color="#888", anchor="middle", mono=True)
    # 公式
    FX = X0 + HS * CW + 50
    fy = Y0 + 10
    lines = [
        ("Y_plane  = dst", "#333", False),
        ("UV_plane = dst + hor_stride × ver_stride = dst + 48", "#333", False),
        ("", "#333", False),
        ("Y = Y_plane[y × hor_stride + x]", C_Y, True),
        ("  = Y_plane[1 × 8 + 3] = 偏移 11 → Y13", C_Y, False),
        ("", "#333", False),
        ("U = UV_plane[(y/2) × hor_stride + (x/2) × 2]", C_U, True),
        ("  = UV_plane[0 × 8 + 1 × 2] = 偏移 48 + 2 = 50 → U01", C_U, False),
        ("", "#333", False),
        ("V = UV_plane[(y/2) × hor_stride + (x/2) × 2 + 1]", C_V, True),
        ("  = 偏移 51 → V01（紧跟在 U 后面）", C_V, False),
    ]
    for t, col, b in lines:
        if t:
            s.text(FX, fy, t, size=12, color=col, mono=True, weight="bold" if b else "normal")
        fy += 24
    fy += 10
    s.text(FX, fy, "• y/2、x/2：4 个像素共用一对 UV，坐标都除以 2", size=12, color="#444"); fy += 22
    s.text(FX, fy, "• × 2：每对 UV 占 2 个字节（U、V 各一个）", size=12, color="#444"); fy += 22
    s.text(FX, fy, "• + 1：V 紧跟在 U 后面（NV21 正好反过来）", size=12, color="#444"); fy += 22
    s.text(FX, fy, "• 1920×1080 时把 8 换成 1920、48 换成 2,088,960 就行", size=12, color="#444")
    s.h = max(fy + 40, Y0 + 9 * (CH + 4) + 50)
    s.save("07_pixel_lookup.svg")


# =====================================================================
# 图 8：NV12 / NV21 / I420 对比（4x4）
# =====================================================================
def fig8():
    s = Svg(1000, 420, "NV12 / NV21 / I420 对比")
    s.text(30, 30, "三种常见的 YUV 4:2:0 格式（4×4 的图，都是 24 字节，区别只在 U、V 怎么放）", size=16, weight="bold")
    bw, bh = 30, 30
    X0 = 260
    rows = [
        ("NV12", "MPP_FMT_YUV420SP", "Y 平面 + UV 交错平面（UVUV）", ["Y"] * 16 + ["U", "V"] * 4),
        ("NV21", "MPP_FMT_YUV420SP_VU", "Y 平面 + VU 交错平面（VUVU）；Android 摄像头常用", ["Y"] * 16 + ["V", "U"] * 4),
        ("I420", "MPP_FMT_YUV420P", "Y 平面 + U 平面 + V 平面（三个分开）", ["Y"] * 16 + ["U"] * 4 + ["V"] * 4),
    ]
    y = 80
    for name, mpp, desc, seq in rows:
        s.text(30, y + bh / 2 - 8, name, size=16, weight="bold")
        s.text(30, y + bh / 2 + 12, mpp, size=10, color="#666", mono=True)
        for i, ch in enumerate(seq):
            fill, col = {"Y": (F_Y, C_Y), "U": (F_U, C_U), "V": (F_V, C_V)}[ch]
            cell(s, X0 + i * bw, y, bw, bh, ch, fill, col, size=12)
        s.text(X0, y + bh + 18, desc, size=12, color="#444")
        # 平面分隔
        s.line(X0 + 16 * bw, y - 6, X0 + 16 * bw, y + bh + 6, color=RED, sw=2)
        if name == "I420":
            s.line(X0 + 20 * bw, y - 6, X0 + 20 * bw, y + bh + 6, color=RED, sw=2)
        y += 96
    s.bracket_h(X0, X0 + 16 * bw, 72, "Y：16 字节（三种格式完全一样）", color=C_Y, size=11)
    s.text(30, y + 6, "SP = Semi-Planar（半平面：Y 一个平面，UV 合在一个平面）；P = Planar（全平面：Y、U、V 三个平面）", size=12, color="#555")
    s.text(30, y + 28, "NV12 和 NV21 读法完全一样，只是 U、V 顺序反了；I420 的 U、V 每行 W/2 字节，行距 hor_stride/2（utils.c 565～592 行）", size=12, color="#555")
    s.h = y + 52
    s.save("08_formats.svg")


for f in (fig1, fig2, fig3, fig4, fig5, fig6, fig7, fig8):
    f()
print("done:", sorted(os.listdir(OUT)))
