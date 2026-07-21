#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Logo 图片转换为黑白点阵 C 数组工具
将 大赛LOGO.jpg 转换为 128x128 黑白点阵,生成 logo.h 和 logo.c 文件
"""

from PIL import Image
import os

def convert_logo_to_bitmap():
    """转换 logo 图片为点阵数据"""
    
    # 读取图片
    print("正在读取图片: 大赛LOGO.jpg")
    img = Image.open('大赛LOGO.jpg')
    
    # 缩放到 128x128
    print("缩放到 128x128...")
    img = img.resize((128, 128), Image.LANCZOS)
    
    # 转换为灰度
    print("转换为灰度图...")
    img = img.convert('L')
    
    # 二值化（阈值 128）: 黑色=0, 白色=1
    # 注意: 在图片中,0=黑(前景),255=白(背景)
    # 但根据需求: 0=黑(前景),1=白(背景)
    print("二值化处理（阈值=128）...")
    img = img.point(lambda x: 0 if x < 128 else 255, mode='L')
    
    # 获取像素数据
    pixels = list(img.getdata())
    
    # 转换为字节流
    # 每行 128 像素 = 16 字节
    # 每字节 8 像素,MSB 在左
    # 0 = 黑色（前景），1 = 白色（背景）
    print("转换为点阵数据...")
    data = []
    for row in range(128):
        for col_byte in range(16):
            byte = 0
            for bit in range(8):
                x = col_byte * 8 + bit
                idx = row * 128 + x
                # 如果像素是白色(255),设置位为1
                if pixels[idx] == 255:
                    byte |= (0x80 >> bit)
            data.append(byte)
    
    return data

def generate_logo_h():
    """生成 logo.h 头文件"""
    header_content = """#ifndef __LOGO_H
#define __LOGO_H

#include <stdint.h>

#define LOGO_WIDTH  128
#define LOGO_HEIGHT 128
#define LOGO_SIZE   2048  /* bytes */

extern const uint8_t logo_bitmap[LOGO_SIZE];

#endif /* __LOGO_H */
"""
    return header_content

def generate_logo_c(data):
    """生成 logo.c 源文件"""
    
    # 格式化字节数组,每行16字节
    lines = []
    for row in range(128):
        start = row * 16
        end = start + 16
        row_bytes = data[start:end]
        hex_values = ', '.join(f'0x{b:02X}' for b in row_bytes)
        lines.append(f'    {hex_values}')
    
    # 移除最后一行的逗号
    if lines:
        lines[-1] = lines[-1].rstrip(',')
    
    array_content = ',\n'.join(lines)
    
    c_content = f"""#include "logo.h"

const uint8_t logo_bitmap[LOGO_SIZE] = {{
    /* 128 行 × 16 字节 = 2048 字节 */
{array_content}
}};
"""
    return c_content

def main():
    """主函数"""
    
    # 检查图片文件是否存在
    if not os.path.exists('大赛LOGO.jpg'):
        print("错误: 找不到文件 '大赛LOGO.jpg'")
        return
    
    # 转换图片
    try:
        data = convert_logo_to_bitmap()
    except Exception as e:
        print(f"转换失败: {e}")
        return
    
    # 定义输出路径
    logo_h_path = os.path.join('Transmit', 'Core', 'Inc', 'logo.h')
    logo_c_path = os.path.join('Transmit', 'Core', 'Src', 'logo.c')
    
    # 生成 logo.h
    print(f"\n生成 {logo_h_path}...")
    header_content = generate_logo_h()
    with open(logo_h_path, 'w', encoding='utf-8') as f:
        f.write(header_content)
    print("logo.h 生成成功!")
    
    # 生成 logo.c
    print(f"\n生成 {logo_c_path}...")
    c_content = generate_logo_c(data)
    with open(logo_c_path, 'w', encoding='utf-8') as f:
        f.write(c_content)
    print("logo.c 生成成功!")
    
    # 验证文件大小
    c_file_size = os.path.getsize(logo_c_path)
    print(f"\nlogo.c 文件大小: {c_file_size} 字节")
    print(f"数据大小: {len(data)} 字节")
    
    print("\n✓ 所有文件生成完成!")

if __name__ == '__main__':
    main()