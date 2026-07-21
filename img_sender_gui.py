#!/usr/bin/env python3
"""
2ASK Image Sender - 图片编码 & 串口发送工具
拖拽图片即可转换，一键串口发送到 TX 端

打包命令:
    pip install pyinstaller pillow pyserial
    pyinstaller --onefile --windowed --icon=NONE --name=2ASK_ImgSender img_sender_gui.py
"""

import os
import sys
import threading
import struct
import time

try:
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
except ImportError:
    print("需要 tkinter (Python 自带)")
    sys.exit(1)

try:
    from PIL import Image, ImageTk
except ImportError:
    print("需要 Pillow: pip install Pillow")
    sys.exit(1)

# ─── 图片编码参数 ───
IMG_WIDTH = 128
IMG_HEIGHT = 128
IMG_BITMAP_SIZE = IMG_WIDTH * IMG_HEIGHT // 8  # 2048
FRAME_DATA_LEN = 64
TOTAL_FRAMES = IMG_BITMAP_SIZE // FRAME_DATA_LEN  # 32

OUTPUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Pic_dir_output")


# ═══════════════════════════════════════════════════════════════════
#  图片编码核心
# ═══════════════════════════════════════════════════════════════════

def img_to_bitmap(img_path, size=128, threshold=128, invert=False):
    """JPG/PNG → 128x128 1-bit bitmap (MSB优先, 0=黑/1=白)"""
    img = Image.open(img_path).convert('L')
    img = img.resize((size, size), Image.LANCZOS)
    pixels = list(img.getdata())
    bitmap = bytearray(size * size // 8)
    for row in range(size):
        for col in range(size):
            gray = pixels[row * size + col]
            bit = 1 if gray >= threshold else 0
            if invert:
                bit = 1 - bit
            byte_idx = row * (size // 8) + col // 8
            bit_pos = 7 - (col % 8)
            if bit:
                bitmap[byte_idx] |= (1 << bit_pos)
            else:
                bitmap[byte_idx] &= ~(1 << bit_pos)
    return bytes(bitmap)


def generate_bin(bitmap_data, width=128, height=128):
    """生成 .bin 文件: 4B头 + bitmap数据"""
    total_frames = len(bitmap_data) // FRAME_DATA_LEN
    header = bytes([0xFE, total_frames, width // 8, height & 0xFF])
    return header + bitmap_data


def bitmap_to_preview_image(bitmap_data, size=128):
    """1-bit bitmap → PIL Image 用于预览"""
    img = Image.new('1', (size, size), 1)
    for row in range(size):
        for col in range(size):
            byte_idx = row * (size // 8) + col // 8
            bit_pos = 7 - (col % 8)
            pixel = (bitmap_data[byte_idx] >> bit_pos) & 1
            img.putpixel((col, row), pixel)
    return img


# ═══════════════════════════════════════════════════════════════════
#  串口发送
# ═══════════════════════════════════════════════════════════════════

def list_serial_ports():
    """列出可用串口"""
    try:
        import serial.tools.list_ports
        ports = serial.tools.list_ports.comports()
        return [p.device for p in sorted(ports)]
    except Exception:
        return []


def send_image_via_serial(bin_data, port, baudrate, log_fn, progress_fn):
    """通过串口发送 .bin 到 TX 的 I 命令，自动处理时序"""
    try:
        import serial
    except ImportError:
        log_fn("[错误] 需要 pyserial: pip install pyserial")
        return False

    try:
        ser = serial.Serial(port, baudrate, timeout=2)
        time.sleep(0.1)
        ser.reset_input_buffer()

        # 1) 发送 I 命令
        log_fn(f"连接 {port} @ {baudrate}bps...")
        ser.write(b'I\n')

        # 等待 TX 回复 I:READY（TX 处理命令需要时间）
        deadline = time.time() + 3.0
        ready = False
        while time.time() < deadline:
            if ser.in_waiting > 0:
                line = ser.readline().decode('ascii', errors='replace').strip()
                log_fn(f"TX> {line}")
                if 'READY' in line:
                    ready = True
                    break
            time.sleep(0.05)

        if not ready:
            log_fn("[错误] TX 未回复 I:READY，请检查连接")
            ser.close()
            return False

        # 2) 发送4字节图像头
        header = bin_data[:4]
        log_fn(f"发送图像头: FE {header[1]:02X} {header[2]:02X} {header[3]:02X}")
        ser.write(header)

        # 等待 TX 回复 I:HDR
        deadline = time.time() + 2.0
        while time.time() < deadline:
            if ser.in_waiting > 0:
                line = ser.readline().decode('ascii', errors='replace').strip()
                log_fn(f"TX> {line}")
                if 'HDR' in line:
                    break
            time.sleep(0.05)

        # 3) 逐帧发送数据
        total_frames = header[1]
        bitmap_data = bin_data[4:]

        for seq in range(total_frames):
            offset = seq * FRAME_DATA_LEN
            chunk = bitmap_data[offset:offset + FRAME_DATA_LEN]
            frame = bytes([seq]) + chunk
            if len(chunk) < FRAME_DATA_LEN:
                frame = bytes([seq]) + chunk + b'\xFF' * (FRAME_DATA_LEN - len(chunk))

            ser.write(frame)
            progress_fn(seq + 1, total_frames)

            # 等待 TX 处理帧
            time.sleep(0.1)
            while ser.in_waiting > 0:
                line = ser.readline().decode('ascii', errors='replace').strip()
                if line:
                    log_fn(f"TX> {line}")

        # 等待最终回复
        time.sleep(0.3)
        while ser.in_waiting > 0:
            line = ser.readline().decode('ascii', errors='replace').strip()
            if line:
                log_fn(f"TX> {line}")

        log_fn("发送完成!")
        ser.close()
        return True

    except Exception as e:
        log_fn(f"[错误] {e}")
        return False


# ═══════════════════════════════════════════════════════════════════
#  GUI 应用
# ═══════════════════════════════════════════════════════════════════

class ImageSenderApp:
    # 配色方案
    BG = "#F5F5F5"
    CARD = "#FFFFFF"
    ACCENT = "#2196F3"        # 蓝色主题
    ACCENT2 = "#4CAF50"       # 绿色成功
    ACCENT3 = "#FF9800"       # 橙色警告
    TEXT = "#212121"
    TEXT2 = "#757575"
    BORDER = "#E0E0E0"

    def __init__(self, root):
        self.root = root
        self.root.title("2ASK Image Sender")
        self.root.geometry("520x640")
        self.root.resizable(False, False)
        self.root.configure(bg=self.BG)

        # 尝试设置窗口图标
        try:
            self.root.iconbitmap(default='')
        except Exception:
            pass

        self.bitmap_data = None
        self.bin_data = None
        self.current_path = None
        self.preview_photo = None  # 保持引用防止GC

        self._build_ui()
        self._center_window()

        # 支持拖拽文件 (Windows)
        self.root.drop_target_register = None
        try:
            import windnd
            windnd.hook_dropfiles(self.root, func=self._on_drop)
        except ImportError:
            pass  # 没有windnd也不影响使用

    def _center_window(self):
        self.root.update_idletasks()
        w = self.root.winfo_width()
        h = self.root.winfo_height()
        x = (self.root.winfo_screenwidth() - w) // 2
        y = (self.root.winfo_screenheight() - h) // 2
        self.root.geometry(f'+{x}+{y}')

    def _build_ui(self):
        # ── 标题 ──
        title_frame = tk.Frame(self.root, bg=self.ACCENT, height=48)
        title_frame.pack(fill=tk.X)
        title_frame.pack_propagate(False)
        tk.Label(title_frame, text="2ASK Image Sender", font=("Segoe UI", 16, "bold"),
                 fg="white", bg=self.ACCENT).pack(expand=True)

        # ── 拖拽区 ──
        self.drop_frame = tk.Frame(self.root, bg=self.CARD, bd=1,
                                    relief=tk.SOLID, highlightbackground=self.BORDER,
                                    highlightthickness=1)
        self.drop_frame.pack(fill=tk.X, padx=16, pady=(16, 8))

        self.drop_label = tk.Label(self.drop_frame,
                                    text="拖拽图片到此处\n或点击选择文件",
                                    font=("Segoe UI", 13), fg=self.TEXT2,
                                    bg=self.CARD, height=4, cursor="hand2")
        self.drop_label.pack(fill=tk.BOTH, padx=20, pady=16)
        self.drop_label.bind("<Button-1>", self._on_browse)

        # ── 参数行 ──
        param_frame = tk.Frame(self.root, bg=self.BG)
        param_frame.pack(fill=tk.X, padx=16, pady=4)

        tk.Label(param_frame, text="阈值:", font=("Segoe UI", 10),
                 fg=self.TEXT, bg=self.BG).pack(side=tk.LEFT)
        self.threshold_var = tk.IntVar(value=200)
        self.threshold_scale = tk.Scale(param_frame, from_=50, to=255,
                                         orient=tk.HORIZONTAL, variable=self.threshold_var,
                                         length=160, bg=self.BG, highlightthickness=0,
                                         troughcolor=self.BORDER,
                                         command=lambda v: self._reencode())
        self.threshold_scale.pack(side=tk.LEFT, padx=(4, 16))

        self.invert_var = tk.BooleanVar(value=False)
        tk.Checkbutton(param_frame, text="反转黑白", variable=self.invert_var,
                        font=("Segoe UI", 10), fg=self.TEXT, bg=self.BG,
                        activebackground=self.BG,
                        command=self._reencode).pack(side=tk.LEFT)

        # ── 预览区 ──
        preview_outer = tk.Frame(self.root, bg=self.CARD, bd=1,
                                  relief=tk.SOLID, highlightbackground=self.BORDER,
                                  highlightthickness=1)
        preview_outer.pack(fill=tk.BOTH, padx=16, pady=8, expand=True)

        # 左: 原图预览
        left = tk.Frame(preview_outer, bg=self.CARD)
        left.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=8, pady=8)
        tk.Label(left, text="原图", font=("Segoe UI", 9), fg=self.TEXT2,
                 bg=self.CARD).pack()
        self.orig_canvas = tk.Canvas(left, width=160, height=160, bg="#EEEEEE",
                                      highlightthickness=0)
        self.orig_canvas.pack(pady=4)

        # 右: 编码预览
        right = tk.Frame(preview_outer, bg=self.CARD)
        right.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True, padx=8, pady=8)
        tk.Label(right, text="编码预览 (128x128)", font=("Segoe UI", 9),
                 fg=self.TEXT2, bg=self.CARD).pack()
        self.enc_canvas = tk.Canvas(right, width=160, height=160, bg="#EEEEEE",
                                     highlightthickness=0)
        self.enc_canvas.pack(pady=4)

        # ── 状态栏 ──
        self.status_var = tk.StringVar(value="就绪 - 等待选择图片")
        self.status_label = tk.Label(self.root, textvariable=self.status_var,
                                      font=("Segoe UI", 9), fg=self.TEXT2,
                                      bg=self.BG, anchor=tk.W)
        self.status_label.pack(fill=tk.X, padx=16, pady=(4, 0))

        # ── 进度条 ──
        self.progress = ttk.Progressbar(self.root, mode='determinate', length=200)
        self.progress.pack(fill=tk.X, padx=16, pady=4)

        # ── 按钮行 ──
        btn_frame = tk.Frame(self.root, bg=self.BG)
        btn_frame.pack(fill=tk.X, padx=16, pady=(4, 16))

        self.save_btn = tk.Button(btn_frame, text="保存 .bin", font=("Segoe UI", 11),
                                   bg=self.ACCENT, fg="white", relief=tk.FLAT,
                                   cursor="hand2", state=tk.DISABLED,
                                   command=self._on_save)
        self.save_btn.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=(0, 4), ipady=4)

        self.send_btn = tk.Button(btn_frame, text="串口发送", font=("Segoe UI", 11),
                                   bg=self.ACCENT2, fg="white", relief=tk.FLAT,
                                   cursor="hand2", state=tk.DISABLED,
                                   command=self._on_send)
        self.send_btn.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=(4, 0), ipady=4)

        # ── 日志区 ──
        log_frame = tk.Frame(self.root, bg=self.CARD, bd=1,
                              relief=tk.SOLID, highlightbackground=self.BORDER,
                              highlightthickness=1)
        log_frame.pack(fill=tk.X, padx=16, pady=(0, 12))
        self.log_text = tk.Text(log_frame, height=4, font=("Consolas", 9),
                                 bg="#263238", fg="#B0BEC5", insertbackground="#B0BEC5",
                                 relief=tk.FLAT, state=tk.DISABLED, wrap=tk.WORD)
        self.log_text.pack(fill=tk.X, padx=4, pady=4)

    # ─── 日志 ───
    def log(self, msg):
        self.log_text.configure(state=tk.NORMAL)
        self.log_text.insert(tk.END, msg + "\n")
        self.log_text.see(tk.END)
        self.log_text.configure(state=tk.DISABLED)

    # ─── 文件操作 ───
    def _on_browse(self, event=None):
        path = filedialog.askopenfilename(
            title="选择图片",
            filetypes=[("图片", "*.jpg *.jpeg *.png *.bmp *.gif *.tiff *.webp")])
        if path:
            self._load_image(path)

    def _on_drop(self, files):
        if files:
            path = files[0].decode('gbk')
            self._load_image(path)

    def _load_image(self, path):
        try:
            self.current_path = path
            self.status_var.set(f"加载: {os.path.basename(path)}")
            self.log(f"加载图片: {path}")
            self._encode()
        except Exception as e:
            self.log(f"[错误] {e}")
            messagebox.showerror("错误", str(e))

    def _encode(self):
        if not self.current_path:
            return

        threshold = self.threshold_var.get()
        invert = self.invert_var.get()

        self.bitmap_data = img_to_bitmap(self.current_path,
                                          threshold=threshold, invert=invert)
        self.bin_data = generate_bin(self.bitmap_data)

        # 统计
        black = sum(1 for b in self.bitmap_data for i in range(8) if not (b >> (7-i) & 1))
        pct = black * 100 // (IMG_WIDTH * IMG_HEIGHT)
        self.status_var.set(
            f"{os.path.basename(self.current_path)} | "
            f"{IMG_WIDTH}x{IMG_HEIGHT} | 黑点 {pct}% | "
            f"{len(self.bin_data)}B | 阈值={threshold}")
        self.log(f"编码完成: {pct}% 黑点, {len(self.bin_data)} bytes")

        # 预览
        self._update_previews()

        # 启用按钮
        self.save_btn.configure(state=tk.NORMAL)
        self.send_btn.configure(state=tk.NORMAL)

    def _reencode(self, *args):
        if self.current_path:
            self._encode()

    def _update_previews(self):
        if not self.current_path or not self.bitmap_data:
            return

        # 原图预览 (缩放到160x160)
        try:
            orig = Image.open(self.current_path).convert('RGB')
            orig = orig.resize((160, 160), Image.LANCZOS)
            self._orig_photo = ImageTk.PhotoImage(orig)
            self.orig_canvas.delete("all")
            self.orig_canvas.create_image(0, 0, anchor=tk.NW, image=self._orig_photo)
        except Exception:
            pass

        # 编码预览: bitmap → PIL Image → 放大到160x160
        try:
            enc_img = bitmap_to_preview_image(self.bitmap_data)
            # 1-bit 放大用 NEAREST 保持像素清晰
            enc_img = enc_img.resize((160, 160), Image.NEAREST)
            # 转 RGB 以便 ImageTk 正常显示
            enc_rgb = enc_img.convert('RGB')
            self._enc_photo = ImageTk.PhotoImage(enc_rgb)
            self.enc_canvas.delete("all")
            self.enc_canvas.create_image(0, 0, anchor=tk.NW, image=self._enc_photo)
        except Exception as e:
            self.log(f"预览生成失败: {e}")

    # ─── 保存 .bin ───
    def _on_save(self):
        if not self.bin_data:
            return
        os.makedirs(OUTPUT_DIR, exist_ok=True)
        basename = os.path.splitext(os.path.basename(self.current_path))[0]
        out_path = os.path.join(OUTPUT_DIR, basename + ".bin")

        # 如果已有同名文件，加数字后缀
        if os.path.exists(out_path):
            i = 1
            while os.path.exists(os.path.join(OUTPUT_DIR, f"{basename}_{i}.bin")):
                i += 1
            out_path = os.path.join(OUTPUT_DIR, f"{basename}_{i}.bin")

        with open(out_path, 'wb') as f:
            f.write(self.bin_data)
        self.log(f"已保存: {out_path}")
        self.status_var.set(f"已保存: {out_path}")

    # ─── 串口发送 ───
    def _on_send(self):
        if not self.bin_data:
            return

        # 弹出串口选择对话框
        ports = list_serial_ports()
        if not ports:
            messagebox.showwarning("无串口", "未检测到串口设备，请检查连接")
            return

        dialog = tk.Toplevel(self.root)
        dialog.title("选择串口")
        dialog.geometry("300x180")
        dialog.configure(bg=self.BG)
        dialog.transient(self.root)
        dialog.grab_set()

        tk.Label(dialog, text="选择串口:", font=("Segoe UI", 11),
                 fg=self.TEXT, bg=self.BG).pack(pady=(16, 4))

        port_var = tk.StringVar(value=ports[0])
        port_combo = ttk.Combobox(dialog, textvariable=port_var,
                                   values=ports, state='readonly', width=20)
        port_combo.pack(pady=4)

        baud_var = tk.StringVar(value="921600")
        tk.Label(dialog, text="波特率:", font=("Segoe UI", 11),
                 fg=self.TEXT, bg=self.BG).pack(pady=(8, 4))
        baud_combo = ttk.Combobox(dialog, textvariable=baud_var,
                                   values=["921600", "115200", "460800"],
                                   state='readonly', width=20)
        baud_combo.pack(pady=4)

        def do_send():
            dialog.destroy()
            port = port_var.get()
            baudrate = int(baud_var.get())
            self._start_send(port, baudrate)

        tk.Button(dialog, text="发送", font=("Segoe UI", 11),
                  bg=self.ACCENT2, fg="white", relief=tk.FLAT,
                  cursor="hand2", command=do_send).pack(pady=12, ipadx=20, ipady=2)

    def _start_send(self, port, baudrate):
        """后台线程发送，避免 GUI 卡死"""
        self.send_btn.configure(state=tk.DISABLED)
        self.save_btn.configure(state=tk.DISABLED)
        self.progress['value'] = 0
        self.progress['maximum'] = TOTAL_FRAMES
        self.log(f"开始发送 → {port} @ {baudrate}bps...")

        def worker():
            def log_fn(msg):
                self.root.after(0, lambda: self.log(msg))
            def progress_fn(cur, total):
                self.root.after(0, lambda: self._update_progress(cur, total))

            ok = send_image_via_serial(self.bin_data, port, baudrate, log_fn, progress_fn)
            self.root.after(0, lambda: self._send_done(ok))

        t = threading.Thread(target=worker, daemon=True)
        t.start()

    def _update_progress(self, cur, total):
        self.progress['value'] = cur
        self.progress['maximum'] = total

    def _send_done(self, ok):
        self.send_btn.configure(state=tk.NORMAL)
        self.save_btn.configure(state=tk.NORMAL)
        if ok:
            self.status_var.set("发送完成!")
        else:
            self.status_var.set("发送失败 - 查看日志")


# ═══════════════════════════════════════════════════════════════════
#  Main
# ═══════════════════════════════════════════════════════════════════

def main():
    root = tk.Tk()
    app = ImageSenderApp(root)

    # 尝试 DPI 感知 (Windows)
    try:
        from ctypes import windll
        windll.shcore.SetProcessDpiAwareness(1)
    except Exception:
        pass

    root.mainloop()


if __name__ == '__main__':
    main()
