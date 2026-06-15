/*
 * Copyright (c) 2006-2022, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __DRV_LCD_H
#define __DRV_LCD_H

/* 1. 替换 RT-Thread 包含文件为标准库 */
#include <stdint.h>    // 提供 uint16_t, uint32_t 等类型
#include <stddef.h>    // 提供 NULL, size_t
#include "main.h"      // 包含 STM32 HAL 库（通常里面有 HAL 定义）

#ifdef PKG_USING_QRCODE
#include "qrcode.h"
#endif

#define LCD_W 240
#define LCD_H 240

// LCD 重要参数集
typedef struct
{
    uint16_t width;         // LCD 宽度
    uint16_t height;        // LCD 高度
    uint16_t id;            // LCD ID
    uint8_t  dir;           // 横屏还是竖屏控制：0，竖屏；1，横屏。
    uint16_t wramcmd;       // 开始写 gram 指令
    uint16_t setxcmd;       // 设置 x 坐标指令
    uint16_t setycmd;       // 设置 y 坐标指令
}_lcd_dev;

// LCD 参数
extern _lcd_dev lcddev; // 管理 LCD 重要参数

// 定义 LCD 控制结构体（根据你的 FSMC 绑定）
typedef struct
{
  __IO uint8_t _u8_REG;
  __IO uint8_t RESERVED;
  __IO uint8_t _u8_RAM;
  __IO uint16_t _u16_RAM;
} LCD_CONTROLLER_TypeDef;

// 颜色定义
#define WHITE            0xFFFF
#define BLACK            0x0000
#define BLUE             0x001F
#define BRED             0XF81F
#define GRED             0XFFE0
#define GBLUE            0X07FF
#define RED              0xF800
#define MAGENTA          0xF81F
#define GREEN            0x07E0
#define CYAN             0x7FFF
#define YELLOW           0xFFE0
#define BROWN            0XBC40
#define BRRED            0XFC07
#define GRAY             0X8430
#define GRAY175          0XAD75
#define GRAY151          0X94B2
#define GRAY187          0XBDD7
#define GRAY240          0XF79E

// 扫描方向定义
#define L2R_U2D  0
#define L2R_D2U  1
#define R2L_U2D  2
#define R2L_D2U  3
#define U2D_L2R  4
#define U2D_R2L  5
#define D2U_L2R  6
#define D2U_R2L  7

/* 2. 核心修改：将所有 rt_err_t 替换为 int，将所有 rt_uintXX 替换为 uintXX */

int drv_lcd_init(void); // 删除了 RT-Thread 的 device 结构体参数
void lcd_clear(uint16_t color);
void lcd_address_set(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void lcd_set_color(uint16_t back, uint16_t fore);

void lcd_draw_point(uint16_t x, uint16_t y);
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r);
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void lcd_draw_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void lcd_fill(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint16_t color);

void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint32_t size);
int lcd_show_string(uint16_t x, uint16_t y, uint32_t size, const char *fmt, ...);
int lcd_show_image(uint16_t x, uint16_t y, uint16_t length, uint16_t wide, const uint8_t *p);

#ifdef PKG_USING_QRCODE
int lcd_show_qrcode(uint16_t x, uint16_t y, uint8_t version, uint8_t ecc, const char *data, uint8_t enlargement);
#endif

void lcd_enter_sleep(void);
void lcd_exit_sleep(void);
void lcd_display_on(void);
void lcd_display_off(void);

void lcd_fill_array(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, void *pcolor);

#ifdef BSP_USING_ONBOARD_LCD_TEAREFFECT
void lcd_teareffect_isr(void);
#endif

#endif /* __DRV_LCD_H */
