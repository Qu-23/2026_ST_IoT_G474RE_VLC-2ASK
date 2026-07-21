#!/usr/bin/env python3
"""
2ASK Image Sender - 图片编码 & 串口发送工具
拖拽图片即可转换，一键串口发送到 TX 端

打包命令:
    cd tools
    pip install pyinstaller pillow pyserial
    pyinstaller --onefile --windowed --name=2ASK_ImgSender img_sender_gui.py
"""

import os
import sys
import time
import threading

try:
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
except ImportError:
    sys.exit(1)

try:
    from PIL import Image, ImageTk, ImageDraw
except ImportError:
    sys.exit(1)

# ─── 常量 ───
IMG_W = 128
IMG_H = 128
BITMAP_SZ = IMG_W * IMG_H // 8
FRAME_DATA = 64
TOTAL_FRAMES = BITMAP_SZ // FRAME_DATA

# 输出目录：与工程根目录的 Pic_dir_output 对齐
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
# 如果脚本在 tools/ 子目录下，向上一级找工程根
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR) if os.path.basename(SCRIPT_DIR) == "tools" else SCRIPT_DIR
OUTPUT_DIR = os.path.join(PROJECT_ROOT, "Pic_dir_output")

# ─── 紫群青色主题 ───
C = {
    "bg":       "#1A1A2E",
    "card":     "#16213E",
    "surface":  "#0F3460",
    "primary":  "#7B2FF7",
    "primary2": "#9D4EDD",
    "accent":   "#C77DFF",
    "cyan":     "#00D2FF",
    "cyan2":    "#0096C7",
    "success":  "#06D6A0",
    "warn":     "#FFD166",
    "error":    "#EF476F",
    "text":     "#EAEAEA",
    "text2":    "#8892B0",
    "border":   "#233554",
    "canvas":   "#0D1B2A",
}


# ═══════════════════════════════════════════
#  图片编码核心
# ═══════════════════════════════════════════

def img_to_bitmap(img_path, size=128, threshold=128, invert=False):
    img = Image.open(img_path).convert('L')
    img = img.resize((size, size), Image.LANCZOS)
    px = list(img.getdata())
    bm = bytearray(size * size // 8)
    for r in range(size):
        for c in range(size):
            bit = (1 if px[r*size+c] >= threshold else 0) ^ invert
            idx = r * (size // 8) + c // 8
            if bit:
                bm[idx] |= (1 << (7 - c % 8))
    return bytes(bm)


def generate_bin(bm, w=128, h=128):
    n = len(bm) // FRAME_DATA
    return bytes([0xFE, n, w // 8, h & 0xFF]) + bm


def bitmap_to_preview(bm, size=128):
    img = Image.new('1', (size, size), 1)
    for r in range(size):
        for c in range(size):
            idx = r * (size // 8) + c // 8
            if not (bm[idx] >> (7 - c % 8) & 1):
                img.putpixel((c, r), 0)
    return img


# ═══════════════════════════════════════════
#  串口
# ═══════════════════════════════════════════

def list_ports():
    try:
        import serial.tools.list_ports
        return [p.device for p in sorted(serial.tools.list_ports.comports())]
    except Exception:
        return []


def serial_send(bin_data, port, baud, log, prog):
    try:
        import serial
    except ImportError:
        log("[错误] pip install pyserial")
        return False
    try:
        s = serial.Serial(port, baud, timeout=2)
        time.sleep(0.1)
        s.reset_input_buffer()

        log(f"连接 {port} @ {baud}bps")
        s.write(b'I\n')

        t0 = time.time()
        while time.time() - t0 < 3:
            if s.in_waiting:
                ln = s.readline().decode('ascii', errors='replace').strip()
                log(f"TX> {ln}")
                if 'READY' in ln:
                    break
        else:
            log("[错误] TX 未回复 READY")
            s.close()
            return False

        hdr = bin_data[:4]
        log(f"发送头: FE {hdr[1]:02X} {hdr[2]:02X} {hdr[3]:02X}")
        s.write(hdr)
        time.sleep(0.1)
        if s.in_waiting:
            ln = s.readline().decode('ascii', errors='replace').strip()
            if ln:
                log(f"TX> {ln}")

        n = hdr[1]
        data = bin_data[4:]
        for seq in range(n):
            off = seq * FRAME_DATA
            chunk = data[off:off + FRAME_DATA]
            frame = bytes([seq]) + chunk
            if len(chunk) < FRAME_DATA:
                frame = bytes([seq]) + chunk + b'\xFF' * (FRAME_DATA - len(chunk))
            s.write(frame)
            prog(seq + 1, n)
            time.sleep(0.1)
            while s.in_waiting:
                ln = s.readline().decode('ascii', errors='replace').strip()
                if ln:
                    log(f"TX> {ln}")

        time.sleep(0.3)
        while s.in_waiting:
            ln = s.readline().decode('ascii', errors='replace').strip()
            if ln:
                log(f"TX> {ln}")

        log("发送完成!")
        s.close()
        return True
    except Exception as e:
        log(f"[错误] {e}")
        return False


# ═══════════════════════════════════════════
#  流线型图标
# ═══════════════════════════════════════════

def make_icon(size=64):
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    margin = 4
    for y in range(size):
        ratio = y / size
        r = int(0 + 123 * ratio)
        g = int(210 - 177 * ratio)
        b = int(255 - 8 * ratio)
        for x in range(margin, size - margin):
            cx, cy = size / 2, size / 2
            radius = size / 2 - margin
            if (x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2:
                img.putpixel((x, y), (r, g, b, 255))
    # 信号波纹
    cx, cy = size // 2, size // 2
    for dy in range(-8, 9):
        for dx in range(-16, 17):
            px, py = cx + dx, cy + dy
            if 0 <= px < size and 0 <= py < size:
                if abs(dy) < 3 * (1 - abs(dx) / 16):
                    img.putpixel((px, py), (255, 255, 255, 200))
    return img


# ═══════════════════════════════════════════
#  GUI
# ═══════════════════════════════════════════

class App:
    def __init__(self, root):
        self.root = root
        self.root.title("2ASK Image Sender")
        self.root.geometry("560x720")
        self.root.minsize(480, 620)
        self.root.configure(bg=C["bg"])

        # 图标
        try:
            icon = make_icon()
            self._icon = ImageTk.PhotoImage(icon)
            self.root.iconphoto(True, self._icon)
        except Exception:
            pass

        self.bm = None
        self.bindata = None
        self.curpath = None

        # 拖拽支持 (tkinterdnd2 或 windnd)
        self._dnd_ok = False
        try:
            from tkinterdnd2 import DND_FILES, TkinterDnD
            # 需要替换 root，这里无法替换，跳过
        except ImportError:
            pass
        try:
            import windnd
            windnd.hook_dropfiles(self.root, func=self._on_drop)
            self._dnd_ok = True
        except ImportError:
            pass

        self._build()
        self._center()

        # 绑定窗口大小变化
        self.root.bind("<Configure>", self._on_resize)

    def _center(self):
        self.root.update_idletasks()
        w = self.root.winfo_width()
        h = self.root.winfo_height()
        self.root.geometry(f'+{(self.root.winfo_screenwidth()-w)//2}+{(self.root.winfo_screenheight()-h)//2}')

    def _on_drop(self, files):
        if files:
            path = files[0].decode('gbk') if isinstance(files[0], bytes) else files[0]
            self._load(path)

    def _on_resize(self, event):
        # 窗口可自由缩放（由 minsize 控制）
        pass

    # ── 构建UI ──
    def _build(self):
        # ── 标题栏 ──
        hdr = tk.Frame(self.root, bg=C["surface"], height=54)
        hdr.pack(fill=tk.X)
        hdr.pack_propagate(False)
        # 底部渐变条
        bar = tk.Frame(hdr, bg=C["primary"], height=3)
        bar.pack(fill=tk.X, side=tk.BOTTOM)
        tk.Label(hdr, text="✦  2ASK  Image  Sender", font=("Segoe UI", 17, "bold"),
                 fg=C["cyan"], bg=C["surface"]).pack(expand=True)

        # ── 拖拽区（带虚线圆角感）──
        df = tk.Frame(self.root, bg=C["card"], highlightbackground=C["primary2"],
                       highlightthickness=2)
        df.pack(fill=tk.X, padx=20, pady=(14, 6))
        dnd_hint = "⬇  拖拽图片到此处  ·  或点击选择" if not self._dnd_ok else "⬇  拖拽图片到此处  ·  或点击选择"
        self.drop_lbl = tk.Label(df, text=dnd_hint,
                                  font=("Segoe UI", 12), fg=C["text2"],
                                  bg=C["card"], height=3, cursor="hand2")
        self.drop_lbl.pack(fill=tk.BOTH, padx=20, pady=12)
        self.drop_lbl.bind("<Button-1>", lambda e: self._browse())
        self.drop_lbl.bind("<Enter>", lambda e: (
            self.drop_lbl.configure(fg=C["accent"], bg=C["surface"]),
            self.drop_lbl.configure(text="⬇  释放鼠标即可加载图片  ·  或点击选择")))
        self.drop_lbl.bind("<Leave>", lambda e: (
            self.drop_lbl.configure(fg=C["text2"], bg=C["card"]),
            self.drop_lbl.configure(text=dnd_hint)))

        # ── 参数行 ──
        pf = tk.Frame(self.root, bg=C["bg"])
        pf.pack(fill=tk.X, padx=20, pady=4)
        tk.Label(pf, text="阈值", font=("Segoe UI", 10), fg=C["text2"], bg=C["bg"]).pack(side=tk.LEFT)
        self.thr_var = tk.IntVar(value=200)
        self.thr_scale = tk.Scale(pf, from_=50, to=255, orient=tk.HORIZONTAL,
                                   variable=self.thr_var, length=200, bg=C["bg"],
                                   fg=C["text"], troughcolor=C["surface"],
                                   highlightthickness=0, sliderrelief=tk.FLAT,
                                   activebackground=C["primary"],
                                   command=lambda v: self._reencode())
        self.thr_scale.pack(side=tk.LEFT, padx=(4, 20))
        self.inv_var = tk.BooleanVar()
        tk.Checkbutton(pf, text="反转", variable=self.inv_var,
                        font=("Segoe UI", 10), fg=C["text2"], bg=C["bg"],
                        selectcolor=C["surface"], activebackground=C["bg"],
                        activeforeground=C["accent"],
                        command=self._reencode).pack(side=tk.LEFT)

        # ── 预览区 ──
        pvf = tk.Frame(self.root, bg=C["card"], highlightbackground=C["border"],
                        highlightthickness=1)
        pvf.pack(fill=tk.BOTH, padx=20, pady=6, expand=True)

        # 左原图
        lf = tk.Frame(pvf, bg=C["card"])
        lf.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=10, pady=10)
        tk.Label(lf, text="◈ 原 图", font=("Segoe UI", 9, "bold"),
                 fg=C["cyan"], bg=C["card"]).pack()
        self.cv_orig = tk.Canvas(lf, width=180, height=180, bg=C["canvas"],
                                  highlightthickness=1, highlightbackground=C["border"])
        self.cv_orig.pack(pady=6, expand=True)

        # 分隔
        sep = tk.Frame(pvf, bg=C["border"], width=2)
        sep.pack(side=tk.LEFT, fill=tk.Y, pady=12, padx=2)

        # 右编码
        rf = tk.Frame(pvf, bg=C["card"])
        rf.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True, padx=10, pady=10)
        tk.Label(rf, text="◈ 编 码", font=("Segoe UI", 9, "bold"),
                 fg=C["primary2"], bg=C["card"]).pack()
        self.cv_enc = tk.Canvas(rf, width=180, height=180, bg=C["canvas"],
                                 highlightthickness=1, highlightbackground=C["border"])
        self.cv_enc.pack(pady=6, expand=True)

        # ── 状态 ──
        self.st_var = tk.StringVar(value="等待选择图片")
        tk.Label(self.root, textvariable=self.st_var, font=("Consolas", 9),
                 fg=C["text2"], bg=C["bg"], anchor=tk.W).pack(fill=tk.X, padx=20, pady=(2, 0))

        # ── 进度 ──
        style = ttk.Style()
        style.theme_use('default')
        style.configure("Purple.Horizontal.TProgressbar",
                         troughcolor=C["surface"], background=C["primary"],
                         darkcolor=C["primary"], lightcolor=C["primary2"],
                         bordercolor=C["bg"])
        self.prog = ttk.Progressbar(self.root, style="Purple.Horizontal.TProgressbar",
                                     mode='determinate')
        self.prog.pack(fill=tk.X, padx=20, pady=4)

        # ── 按钮行 ──
        bf = tk.Frame(self.root, bg=C["bg"])
        bf.pack(fill=tk.X, padx=20, pady=(2, 6))

        self.btn_save = tk.Button(bf, text="✦ 保存 .bin", font=("Segoe UI", 12, "bold"),
                                   bg=C["primary"], fg="white", relief=tk.FLAT,
                                   activebackground=C["primary2"], activeforeground="white",
                                   cursor="hand2", state=tk.DISABLED, command=self._save,
                                   bd=0, padx=10, pady=6)
        self.btn_save.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=(0, 6))

        self.btn_send = tk.Button(bf, text="◈ 串口发送", font=("Segoe UI", 12, "bold"),
                                   bg=C["cyan2"], fg="white", relief=tk.FLAT,
                                   activebackground=C["cyan"], activeforeground=C["bg"],
                                   cursor="hand2", state=tk.DISABLED, command=self._send,
                                   bd=0, padx=10, pady=6)
        self.btn_send.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=(6, 0))

        # ── 日志 ──
        logf = tk.Frame(self.root, bg=C["canvas"], highlightbackground=C["border"],
                         highlightthickness=1)
        logf.pack(fill=tk.BOTH, padx=20, pady=(0, 12), expand=False)
        self.logw = tk.Text(logf, height=4, font=("Consolas", 9),
                             bg=C["canvas"], fg=C["text2"], insertbackground=C["text2"],
                             relief=tk.FLAT, state=tk.DISABLED, wrap=tk.WORD,
                             selectbackground=C["primary"])
        self.logw.pack(fill=tk.BOTH, padx=4, pady=4)

    # ── 日志 ──
    def log(self, m):
        self.logw.configure(state=tk.NORMAL)
        self.logw.insert(tk.END, m + "\n")
        self.logw.see(tk.END)
        self.logw.configure(state=tk.DISABLED)

    # ── 文件 ──
    def _browse(self):
        p = filedialog.askopenfilename(
            title="选择图片",
            filetypes=[("图片", "*.jpg *.jpeg *.png *.bmp *.gif *.tiff *.webp")])
        if p:
            self._load(p)

    def _load(self, path):
        try:
            self.curpath = path
            self.log(f"加载: {os.path.basename(path)}")
            self._encode()
        except Exception as e:
            self.log(f"[错误] {e}")

    def _encode(self):
        if not self.curpath:
            return
        thr = self.thr_var.get()
        inv = self.inv_var.get()
        self.bm = img_to_bitmap(self.curpath, threshold=thr, invert=inv)
        self.bindata = generate_bin(self.bm)
        bk = sum(1 for b in self.bm for i in range(8) if not (b >> (7 - i) & 1))
        pct = bk * 100 // (IMG_W * IMG_H)
        self.st_var.set(f"{os.path.basename(self.curpath)}  |  {IMG_W}x{IMG_H}  |  黑{pct}%  |  {len(self.bindata)}B  |  阈值{thr}")
        self.log(f"编码完成: {pct}%黑, {len(self.bindata)}B")
        self._preview()
        self.btn_save.configure(state=tk.NORMAL)
        self.btn_send.configure(state=tk.NORMAL)

    def _reencode(self, *_):
        if self.curpath:
            self._encode()

    def _preview(self):
        if not self.curpath or not self.bm:
            return
        try:
            orig = Image.open(self.curpath).convert('RGB').resize((180, 180), Image.LANCZOS)
            self._ph_o = ImageTk.PhotoImage(orig)
            self.cv_orig.delete("all")
            self.cv_orig.create_image(0, 0, anchor=tk.NW, image=self._ph_o)
        except Exception:
            pass
        try:
            enc = bitmap_to_preview(self.bm).resize((180, 180), Image.NEAREST).convert('RGB')
            self._ph_e = ImageTk.PhotoImage(enc)
            self.cv_enc.delete("all")
            self.cv_enc.create_image(0, 0, anchor=tk.NW, image=self._ph_e)
        except Exception as e:
            self.log(f"预览失败: {e}")

    # ── 保存 ──
    def _save(self):
        if not self.bindata:
            return
        os.makedirs(OUTPUT_DIR, exist_ok=True)
        base = os.path.splitext(os.path.basename(self.curpath))[0]
        p = os.path.join(OUTPUT_DIR, base + ".bin")
        i = 1
        while os.path.exists(p):
            p = os.path.join(OUTPUT_DIR, f"{base}_{i}.bin")
            i += 1
        with open(p, 'wb') as f:
            f.write(self.bindata)
        self.log(f"保存: {p}")
        self.st_var.set(f"已保存 → {p}")

    # ── 发送 ──
    def _send(self):
        if not self.bindata:
            return
        ports = list_ports()
        if not ports:
            messagebox.showwarning("无串口", "未检测到串口设备")
            return

        dlg = tk.Toplevel(self.root)
        dlg.title("串口发送")
        dlg.geometry("340x240")
        dlg.configure(bg=C["bg"])
        dlg.transient(self.root)
        dlg.grab_set()
        dlg.resizable(False, False)

        tk.Label(dlg, text="◈ 串口配置", font=("Segoe UI", 13, "bold"),
                 fg=C["cyan"], bg=C["bg"]).pack(pady=(16, 8))

        row1 = tk.Frame(dlg, bg=C["bg"])
        row1.pack(fill=tk.X, padx=24)
        tk.Label(row1, text="端口", font=("Segoe UI", 10), fg=C["text2"], bg=C["bg"]).pack(side=tk.LEFT)
        pv = tk.StringVar(value=ports[0])
        ttk.Combobox(row1, textvariable=pv, values=ports, state='readonly', width=18).pack(side=tk.RIGHT)

        row2 = tk.Frame(dlg, bg=C["bg"])
        row2.pack(fill=tk.X, padx=24, pady=(8, 0))
        tk.Label(row2, text="波特率", font=("Segoe UI", 10), fg=C["text2"], bg=C["bg"]).pack(side=tk.LEFT)
        bv = tk.StringVar(value="921600")
        ttk.Combobox(row2, textvariable=bv, values=["921600", "460800", "115200"],
                      state='readonly', width=18).pack(side=tk.RIGHT)

        def go():
            dlg.destroy()
            self._start_tx(pv.get(), int(bv.get()))

        # 发送按钮：足够大，不被遮挡
        btn = tk.Button(dlg, text="◈  开始发送", font=("Segoe UI", 13, "bold"),
                  bg=C["cyan2"], fg="white", relief=tk.FLAT, cursor="hand2",
                  activebackground=C["cyan"], command=go, bd=0)
        btn.pack(pady=20, ipadx=32, ipady=6)

    def _start_tx(self, port, baud):
        self.btn_send.configure(state=tk.DISABLED)
        self.btn_save.configure(state=tk.DISABLED)
        self.prog['value'] = 0
        self.prog['maximum'] = TOTAL_FRAMES

        def w():
            ok = serial_send(self.bindata, port, baud,
                             lambda m: self.root.after(0, lambda: self.log(m)),
                             lambda c, t: self.root.after(0, lambda: self._prog(c, t)))
            self.root.after(0, lambda: self._done(ok))

        threading.Thread(target=w, daemon=True).start()

    def _prog(self, c, t):
        self.prog['value'] = c
        self.prog['maximum'] = t

    def _done(self, ok):
        self.btn_send.configure(state=tk.NORMAL)
        self.btn_save.configure(state=tk.NORMAL)
        self.st_var.set("发送完成!" if ok else "发送失败")


def main():
    try:
        from ctypes import windll
        windll.shcore.SetProcessDpiAwareness(1)
    except Exception:
        pass
    root = tk.Tk()
    App(root)
    root.mainloop()


if __name__ == '__main__':
    main()
