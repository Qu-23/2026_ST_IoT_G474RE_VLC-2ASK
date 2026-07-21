#!/usr/bin/env python3
"""
send_img.py - 实时图片编码工具：将 JPG/PNG 转换为可直接串口发送的 .bin 文件

输出格式（与 TX 端 I 命令兼容）：
  文件 = 图像头(4B) + bitmap数据(2048B)
    头: [0xFE, total_frames, bytes_per_row, height]
    数据: 128x128 1-bit bitmap, MSB优先, 0=黑/1=白

TX 端 I 命令收到此 .bin 文件后：
  1. 读前4字节作为图像头，封装为 ASK_TYPE_GRAPHIC 帧发送
  2. 后续数据按64字节分包+1字节seq，封装为数据帧发送

用法：
    python send_img.py input.jpg                    # 默认输出到 Pic_dir_output/
    python send_img.py input.jpg -o output.bin      # 指定输出路径
    python send_img.py input.jpg --threshold 200    # 指定二值化阈值
    python send_img.py input.jpg --invert           # 反转黑白
    python send_img.py --batch .                    # 批量转换目录下所有图片
    python send_img.py input.jpg --send COM3        # 编码后直接通过串口发送
"""

import sys
import os
import time
import struct
import argparse

try:
    from PIL import Image
except ImportError:
    print("需要 Pillow 库: pip install Pillow")
    sys.exit(1)

# 默认输出目录：兼容 PyInstaller 打包
def _get_project_root():
    if getattr(sys, 'frozen', False):
        exe_dir = os.path.dirname(sys.executable)
        if os.path.basename(exe_dir).lower() == 'tools':
            return os.path.dirname(exe_dir)
        return exe_dir
    else:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        if os.path.basename(script_dir) == "tools":
            return os.path.dirname(script_dir)
        return script_dir

PROJECT_ROOT = _get_project_root()
DEFAULT_OUTPUT_DIR = os.path.join(PROJECT_ROOT, "Pic_dir_output")

IMG_WIDTH = 128
IMG_HEIGHT = 128
IMG_BYTES_PER_ROW = IMG_WIDTH // 8  # 16
IMG_BITMAP_SIZE = IMG_WIDTH * IMG_HEIGHT // 8  # 2048
FRAME_DATA_LEN = 64  # 每帧数据载荷字节数
TOTAL_FRAMES = IMG_BITMAP_SIZE // FRAME_DATA_LEN  # 32


def img_to_bitmap(img_path, size=128, threshold=128, invert=False):
    """将图片转换为 1-bit bitmap 数据"""
    img = Image.open(img_path)
    img = img.convert('L')
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


def generate_send_file(bitmap_data, width=128, height=128):
    """生成可直接串口发送的 .bin 文件

    格式: [图像头4B] + [bitmap数据]
      头: 0xFE, total_frames, bytes_per_row, height
      数据: width*height/8 字节的 1-bit bitmap
    """
    total_frames = len(bitmap_data) // FRAME_DATA_LEN
    if len(bitmap_data) % FRAME_DATA_LEN != 0:
        total_frames += 1

    header = bytes([
        0xFE,                          # 图像头标记
        total_frames,                  # 数据帧总数
        width // 8,                    # 每行字节数
        height & 0xFF                  # 图像高度
    ])

    return header + bitmap_data


def process_image(input_path, output_path, threshold=128, invert=False):
    """处理单张图片：转换 + 生成 .bin 文件"""
    basename = os.path.splitext(os.path.basename(input_path))[0]
    print(f'转换: {input_path}')

    bitmap = img_to_bitmap(input_path, threshold=threshold, invert=invert)

    # 统计
    black_count = sum(1 for b in bitmap for i in range(8) if not (b >> (7-i) & 1))
    total_pixels = IMG_WIDTH * IMG_HEIGHT
    pct = black_count * 100 // total_pixels
    print(f'  像素: {black_count}/{total_pixels} 黑点 ({pct}%), 阈值={threshold}')

    # 生成发送文件
    send_data = generate_send_file(bitmap)
    print(f'  输出: {output_path} ({len(send_data)} bytes = 4B头 + {len(bitmap)}B数据)')

    # 确保输出目录存在
    out_dir = os.path.dirname(output_path)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    with open(output_path, 'wb') as f:
        f.write(send_data)

    print(f'  完成!')
    return send_data


def send_via_serial(bin_data, port, baudrate=921600):
    """通过串口将 .bin 文件发送给 TX 端 I 命令

    流程:
      1. 打开串口，发送 "I\\n" 进入图像接收模式
      2. 等待 TX 回复 "I:READY"
      3. 发送4字节图像头
      4. 等待 TX 回复 "I:HDR"
      5. 逐帧发送65字节数据帧（1B seq + 64B data），每帧间隔100ms
      6. 等待 TX 回复 "I:OK"
    """
    try:
        import serial
    except ImportError:
        print("需要 pyserial 库: pip install pyserial")
        sys.exit(1)

    print(f'\n串口发送: {port} @ {baudrate}bps')

    ser = serial.Serial(port, baudrate, timeout=2)
    time.sleep(0.1)  # 等待串口稳定

    # 清空接收缓冲
    ser.reset_input_buffer()

    # 1. 发送 I 命令
    print('  发送 I 命令...')
    ser.write(b'I\n')
    time.sleep(0.2)

    resp = ser.readline().decode('ascii', errors='replace').strip()
    print(f'  TX: {resp}')
    if 'READY' not in resp:
        print(f'  错误: TX 未就绪，回复: {resp}')
        ser.close()
        return

    # 2. 发送4字节图像头
    header = bin_data[:4]
    print(f'  发送图像头: {header.hex().upper()}')
    ser.write(header)

    # 等待 TX 处理头帧（含 2ASK 发送延迟 ~50ms）
    time.sleep(0.1)
    resp = ser.readline().decode('ascii', errors='replace').strip()
    print(f'  TX: {resp}')

    # 3. 逐帧发送数据
    total_frames = header[1]
    bitmap_data = bin_data[4:]

    for seq in range(total_frames):
        offset = seq * FRAME_DATA_LEN
        chunk = bitmap_data[offset:offset + FRAME_DATA_LEN]

        # 构造65字节数据帧: [seq] + [64B data]
        frame = bytes([seq]) + chunk
        if len(chunk) < FRAME_DATA_LEN:
            frame = bytes([seq]) + chunk + b'\xFF' * (FRAME_DATA_LEN - len(chunk))

        ser.write(frame)

        # 等待 TX 处理帧（2ASK发送~30ms + idle~30ms）
        time.sleep(0.1)

        # 读取 TX 回复（非阻塞式）
        while ser.in_waiting > 0:
            resp = ser.readline().decode('ascii', errors='replace').strip()
            if resp:
                print(f'  TX: {resp}')

        print(f'  帧 {seq+1}/{total_frames} 已发送', end='\r')

    print(f'\n  全部 {total_frames} 帧已发送!')

    # 等待最终回复
    time.sleep(0.3)
    while ser.in_waiting > 0:
        resp = ser.readline().decode('ascii', errors='replace').strip()
        if resp:
            print(f'  TX: {resp}')

    ser.close()
    print('  串口已关闭')


def main():
    parser = argparse.ArgumentParser(
        description='实时图片编码：JPG/PNG → 可串口发送的 .bin 文件')
    parser.add_argument('input', nargs='?', help='输入图片路径 (JPG/PNG)')
    parser.add_argument('-o', '--output', default=None,
                        help='输出 .bin 文件路径 (默认: Pic_dir_output/<输入名>.bin)')
    parser.add_argument('--threshold', type=int, default=200,
                        help='二值化阈值 0-255 (默认: 200, 适合偏亮图片)')
    parser.add_argument('--invert', action='store_true',
                        help='反转黑白')
    parser.add_argument('--batch', metavar='DIR', default=None,
                        help='批量转换目录下所有图片文件')
    parser.add_argument('-d', '--output-dir', default=DEFAULT_OUTPUT_DIR,
                        help=f'输出目录 (默认: {DEFAULT_OUTPUT_DIR})')
    parser.add_argument('--send', metavar='PORT', default=None,
                        help='编码后直接通过串口发送 (如 --send COM3)')
    parser.add_argument('--baudrate', type=int, default=921600,
                        help='串口波特率 (默认: 921600)')

    args = parser.parse_args()

    # 批量模式
    if args.batch:
        batch_dir = args.batch
        if not os.path.isdir(batch_dir):
            print(f'错误: 目录不存在 - {batch_dir}')
            sys.exit(1)

        exts = ('.jpg', '.jpeg', '.png', '.bmp', '.gif', '.tiff', '.webp')
        files = [f for f in os.listdir(batch_dir)
                 if f.lower().endswith(exts)]

        if not files:
            print(f'目录中没有图片文件: {batch_dir}')
            sys.exit(0)

        print(f'批量转换: {len(files)} 张图片 → {args.output_dir}')
        os.makedirs(args.output_dir, exist_ok=True)

        for f in sorted(files):
            input_path = os.path.join(batch_dir, f)
            basename = os.path.splitext(f)[0]
            output_path = os.path.join(args.output_dir, basename + '.bin')
            try:
                process_image(input_path, output_path,
                             threshold=args.threshold, invert=args.invert)
            except Exception as e:
                print(f'  错误: {e}')

        print(f'\n批量转换完成！输出目录: {args.output_dir}')
        return

    # 单文件模式
    if not args.input:
        parser.print_help()
        print('\n示例:')
        print('  python send_img.py photo.jpg                   # 生成 .bin 文件')
        print('  python send_img.py photo.jpg --threshold 180   # 调整二值化阈值')
        print('  python send_img.py photo.jpg --send COM3       # 编码+串口发送')
        print('  python send_img.py --batch .                   # 批量转换当前目录')
        sys.exit(0)

    if args.output:
        output_path = args.output
    else:
        basename = os.path.splitext(os.path.basename(args.input))[0]
        os.makedirs(args.output_dir, exist_ok=True)
        output_path = os.path.join(args.output_dir, basename + '.bin')

    bin_data = process_image(args.input, output_path,
                             threshold=args.threshold, invert=args.invert)

    # 串口发送模式
    if args.send:
        send_via_serial(bin_data, args.send, baudrate=args.baudrate)


if __name__ == '__main__':
    main()
