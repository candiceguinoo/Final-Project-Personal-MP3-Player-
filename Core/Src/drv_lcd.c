/*
 * Copyright (c) 2006-2022, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2021-12-28     unknow       copy by STemwin
 * 2021-12-29     xiangxistu   port for lvgl <lcd_fill_array>
 * 2022-6-26      solar        Improve the api required for resistive touch screen calibration
 * 2023-05-17     yuanjie      parallel driver improved
 * * [Modified for Bare Metal STM32]
 */

#include "main.h" // 替换 board.h，引入 HAL 库
#include "drv_lcd.h"
#include "drv_lcd_font.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

// 屏蔽 RT-Thread 的日志系统
#define LOG_D(...)
#define LOG_E(...)
#define LOG_I(...)

// 移除原有的撕裂效果宏引脚定义，改为裸机
#ifdef BSP_USING_ONBOARD_LCD_TEAREFFECT
    #define LCD_TE_PORT GPIOD
    #define LCD_TE_PIN  GPIO_PIN_6
#endif

_lcd_dev lcddev;
uint16_t BACK_COLOR = 0xFFFF, FORE_COLOR = 0x0000; // 替换了宏定义，使用 16 进制颜色

#define LCD_CLEAR_SEND_NUMBER 5760

// 裸机背光引脚定义 (PF9)
#define LCD_BL_PORT GPIOF
#define LCD_BL_PIN  GPIO_PIN_9

// 裸机复位引脚定义 (PD3)
#define LCD_RST_PORT GPIOD
#define LCD_RST_PIN  GPIO_PIN_3

#define LCD_BASE ((uint32_t)(0x68000000 | 0x0003FFFE)) // A18 link to DCX
#define LCD ((LCD_CONTROLLER_TypeDef *)LCD_BASE)

// 移除了 struct rt_device 等 RTOS 设备树相关的结构体继承
struct drv_lcd_device
{
    // 裸机环境下仅保留所需的信息结构，去除了 rt_device parent
    uint8_t bits_per_pixel;
    uint8_t pixel_format;
};

static struct drv_lcd_device _lcd;

// 写寄存器函数
// regval:寄存器值
void LCD_WR_REG(uint8_t regval)
{
    LCD->_u8_REG = regval; // 写入要写的寄存器序号
}

// 写LCD数据
// data:要写入的值
// 【关键修改：针对你的 8位 硬件总线自动拆分 16 位数据】
void LCD_WR_DATA16(uint16_t data)
{
    LCD->_u8_RAM = (uint8_t)(data >> 8);
    LCD->_u8_RAM = (uint8_t)(data & 0xFF);
}

void LCD_WR_DATA8(uint8_t data)
{
    LCD->_u8_RAM = data;
}

// 读LCD数据
// 返回值:读到的值
uint8_t LCD_RD_DATA8(void)
{
    return LCD->_u8_RAM;
}

// 写寄存器
// LCD_Reg:寄存器地址
// LCD_RegValue:要写入的数据
void LCD_WriteReg(uint8_t LCD_Reg, uint16_t LCD_RegValue)
{
    LCD->_u8_REG = LCD_Reg;      // 写入要写的寄存器序号
    LCD_WR_DATA16(LCD_RegValue); // 使用拆分后的 16位 写入函数
}

// 读寄存器
// LCD_Reg:寄存器地址
// 返回值:读到的数据
uint16_t LCD_ReadReg(uint16_t LCD_Reg)
{
    LCD_WR_REG(LCD_Reg);  // 写入要读的寄存器序号
    return LCD_RD_DATA8(); // 返回读到的值
}

// 开始写GRAM
void LCD_WriteRAM_Prepare(void)
{
    LCD->_u8_REG = lcddev.wramcmd;
}

// LCD写GRAM
// RGB_Code:颜色值
void LCD_WriteRAM(uint16_t RGB_Code)
{
    LCD_WR_DATA16(RGB_Code); // 写十六位GRAM
}

// 从ILI93xx读出的数据为GBR格式，而我们写入的时候为RGB格式。
// 通过该函数转换
// c:GBR格式的颜色值
// 返回值：RGB格式的颜色值
uint16_t LCD_BGR2RGB(uint16_t c)
{
    uint16_t r, g, b, rgb;
    b = (c >> 0) & 0x1f;
    g = (c >> 5) & 0x3f;
    r = (c >> 11) & 0x1f;
    rgb = (b << 11) + (g << 5) + (r << 0);
    return (rgb);
}

// 设置光标位置(对RGB屏无效)
// Xpos:横坐标
// Ypos:纵坐标
void LCD_SetCursor(uint16_t Xpos, uint16_t Ypos)
{
    LCD_WR_REG(lcddev.setxcmd);
    LCD_WR_DATA16(Xpos >> 8);
    LCD_WR_DATA16(Xpos & 0XFF);
    LCD_WR_REG(lcddev.setycmd);
    LCD_WR_DATA16(Ypos >> 8);
    LCD_WR_DATA16(Ypos & 0XFF);
}

// 读取个某点的颜色值
// x,y:坐标
// 返回值:此点的颜色
void LCD_ReadPoint(char *pixel, int x, int y)
{
    uint16_t *color = (uint16_t *)pixel;
    uint16_t r = 0, g = 0, b = 0;
    if (x >= lcddev.width || y >= lcddev.height)
    {
        *color = 0; // 超过了范围,直接返回
        return;
    }
    LCD_SetCursor(x, y);

    LCD_WR_REG(0X2E); // 9341/3510/1963 发送读GRAM指令

    r = LCD_RD_DATA8();      // dummy Read
    r = LCD_RD_DATA8(); // 实际坐标颜色
    b = LCD_RD_DATA8();

    g = r & 0XFF; // 对于9341/5310/5510,第一次读取的是RG的值,R在前,G在后,各占8位
    g <<= 8;
    *color = (((r >> 11) << 11) | ((g >> 10) << 5) | (b >> 11)); // ILI9341/NT35310/NT35510需要公式转换一下
}

// LCD开启显示
void LCD_DisplayOn(void)
{
    LCD_WR_REG(0X29); // 开启显示
}

// LCD关闭显示
void LCD_DisplayOff(void)
{
    LCD_WR_REG(0X28); // 关闭显示
}

// 初始化LCD背光定时器 (裸机下替换为普通 GPIO 控制)
void LCD_PWM_BackLightInit()
{
    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_SET);
}

// 设置LCD背光亮度 (裸机下替换为简单的开关逻辑)
// pwm:背光等级,0~100.越大越亮.
void LCD_BackLightSet(uint8_t value)
{
    value = value > 100 ? 100 : value;
    if(value > 0)
    {
        HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_SET);
    }
    else
    {
        HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_RESET);
    }
}

// 设置LCD的自动扫描方向(对RGB屏无效)
// 注意:其他函数可能会受到此函数设置的影响(尤其是9341),
// 所以,一般设置为L2R_U2D即可,如果设置为其他扫描方式,可能导致显示不正常.
// dir:0~7,代表8个方向(具体定义见lcd.h)
// TODO 刷新方向
void LCD_Scan_Dir(uint8_t dir)
{

}

// 快速画点
// x,y:坐标
// color:颜色
static void LCD_Fast_DrawPoint(const char *pixel, int x, int y)
{
    uint16_t color = *((uint16_t *)pixel);

    LCD_WR_REG(lcddev.setxcmd);
    LCD_WR_DATA16(x >> 8);
    LCD_WR_DATA16(x & 0XFF);
    LCD_WR_REG(lcddev.setycmd);
    LCD_WR_DATA16(y >> 8);
    LCD_WR_DATA16(y & 0XFF);
    LCD->_u8_REG = lcddev.wramcmd;
    LCD_WR_DATA16(color); // 确保通过拆分函数写入
}

// 设置LCD显示方向
// dir:0,竖屏；1,横屏
void LCD_Display_Dir(uint8_t dir)
{
    lcddev.dir = dir; // 竖屏/横屏
    if (dir == 0)     // 竖屏
    {
        lcddev.width = 240;
        lcddev.height = 240;

        lcddev.wramcmd = 0X2C;
        lcddev.setxcmd = 0X2A;
        lcddev.setycmd = 0X2B;
    }
    else // 横屏
    {
        lcddev.width = 240;
        lcddev.height = 240;

        lcddev.wramcmd = 0X2C;
        lcddev.setxcmd = 0X2A;
        lcddev.setycmd = 0X2B;
    }
    // TODO scan dir settings
    // LCD_Scan_Dir(DFT_SCAN_DIR); //默认扫描方向
}

static int lcd_write_half_word(const uint16_t da)
{
    LCD_WR_DATA16(da);
    return 0;
}

static int lcd_write_data_buffer(const void *send_buf, size_t length) // 替换 rt_size_t
{
    uint8_t *pdata = NULL;
    size_t len = 0;

    pdata = (uint8_t*)send_buf;
    len = length;

    if (pdata != NULL)
    {
        while (len -- )
        {
            LCD_WR_DATA8(*pdata);
            pdata ++;
        }
    }
    return 0;
}

/**
 * Set background color and foreground color
 *
 * @param   back    background color
 * @param   fore    fore color
 *
 * @return  void
 */
void lcd_set_color(uint16_t back, uint16_t fore) // 替换 rt_uint16_t
{
    BACK_COLOR = back;
    FORE_COLOR = fore;
}

/**
 * Set drawing area
 *
 * @param   x1      start of x position
 * @param   y1      start of y position
 * @param   x2      end of x position
 * @param   y2      end of y position
 *
 * @return  void
 */
void lcd_address_set(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{

    LCD_WR_REG(lcddev.setxcmd);
    LCD_WR_DATA8(x1 >> 8);
    LCD_WR_DATA8(x1 & 0xff);
    LCD_WR_DATA8(x2 >> 8);
    LCD_WR_DATA8(x2 & 0xff);
    LCD_WR_REG(lcddev.setycmd);
    LCD_WR_DATA8(y1 >> 8);
    LCD_WR_DATA8(y1 & 0xff);
    LCD_WR_DATA8(y2 >> 8);
    LCD_WR_DATA8(y2 & 0xff);

    LCD_WriteRAM_Prepare();      // 开始写入GRAM
}

/**
 * clear the lcd.
 *
 * @param   color       Fill color
 *
 * @return  void
 */
void lcd_clear(uint16_t color)
{
    uint32_t index = 0;
    uint32_t totalpoint = lcddev.width;
    totalpoint *= lcddev.height; // 得到总点数
    lcd_address_set(0, 0, lcddev.width - 1, lcddev.height - 1); // 设置光标位置
    for (index = 0; index < totalpoint; index++)
    {
        lcd_write_half_word(color);
    }
}

/**
 * display a point on the lcd.
 *
 * @param   x   x position
 * @param   y   y position
 *
 * @return  void
 */
void lcd_draw_point(uint16_t x, uint16_t y)
{
    lcd_address_set(x, y, x, y);
    lcd_write_half_word(FORE_COLOR);
}


/**
 * full color on the lcd.
 *
 * @param   x_start     start of x position
 * @param   y_start     start of y position
 * @param   x_end       end of x position
 * @param   y_end       end of y position
 * @param   color       Fill color
 *
 * @return  void
 */
void lcd_fill(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint16_t color)
{
    uint16_t i = 0, j = 0;
    uint32_t size = 0, size_remain = 0;
    uint8_t *fill_buf = NULL;

    size = (x_end - x_start) * (y_end - y_start) * 2;

    if (size > LCD_CLEAR_SEND_NUMBER)
    {
        /* the number of remaining to be filled */
        size_remain = size - LCD_CLEAR_SEND_NUMBER;
        size = LCD_CLEAR_SEND_NUMBER;
    }

    lcd_address_set(x_start, y_start, x_end, y_end);

    fill_buf = (uint8_t *)malloc(size); // 使用标准 C malloc
    if (fill_buf)
    {
        /* fast fill */
        while (1)
        {
            for (i = 0; i < size / 2; i++)
            {
                fill_buf[2 * i] = color >> 8;
                fill_buf[2 * i + 1] = color & 0xFF; // 增加位与保障
            }
            lcd_write_data_buffer(fill_buf, size);

            /* Fill completed */
            if (size_remain == 0)
                break;

            /* calculate the number of fill next time */
            if (size_remain > LCD_CLEAR_SEND_NUMBER)
            {
                size_remain = size_remain - LCD_CLEAR_SEND_NUMBER;
            }
            else
            {
                size = size_remain;
                size_remain = 0;
            }
        }
        free(fill_buf); // 使用标准 C free
    }
    else
    {
        for (i = y_start; i <= y_end; i++)
        {
            for (j = x_start; j <= x_end; j++)lcd_write_half_word(color);
        }
    }
}

/**
 * display a line on the lcd.
 *
 * @param   x1      x1 position
 * @param   y1      y1 position
 * @param   x2      x2 position
 * @param   y2      y2 position
 *
 * @return  void
 */
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    uint16_t t;
    uint32_t i = 0;
    int xerr = 0, yerr = 0, delta_x, delta_y, distance;
    int incx, incy, row, col;

    if (y1 == y2)
    {
        /* fast draw transverse line */
        lcd_address_set(x1, y1, x2, y2);

        uint8_t line_buf[480] = {0};

        for (i = 0; i < x2 - x1; i++)
        {
            line_buf[2 * i] = FORE_COLOR >> 8;
            line_buf[2 * i + 1] = FORE_COLOR & 0xFF;
        }

        lcd_write_data_buffer(line_buf, (x2 - x1) * 2);

        return ;
    }

    delta_x = x2 - x1;
    delta_y = y2 - y1;
    row = x1;
    col = y1;
    if (delta_x > 0)incx = 1;
    else if (delta_x == 0)incx = 0;
    else
    {
        incx = -1;
        delta_x = -delta_x;
    }
    if (delta_y > 0)incy = 1;
    else if (delta_y == 0)incy = 0;
    else
    {
        incy = -1;
        delta_y = -delta_y;
    }
    if (delta_x > delta_y)distance = delta_x;
    else distance = delta_y;
    for (t = 0; t <= distance + 1; t++)
    {
        lcd_draw_point(row, col);
        xerr += delta_x ;
        yerr += delta_y ;
        if (xerr > distance)
        {
            xerr -= distance;
            row += incx;
        }
        if (yerr > distance)
        {
            yerr -= distance;
            col += incy;
        }
    }
}

/**
 * display a rectangle on the lcd.
 *
 * @param   x1      x1 position
 * @param   y1      y1 position
 * @param   x2      x2 position
 * @param   y2      y2 position
 *
 * @return  void
 */
void lcd_draw_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    lcd_draw_line(x1, y1, x2, y1);
    lcd_draw_line(x1, y1, x1, y2);
    lcd_draw_line(x1, y2, x2, y2);
    lcd_draw_line(x2, y1, x2, y2);
}

/**
 * display a circle on the lcd.
 *
 * @param   x       x position of Center
 * @param   y       y position of Center
 * @param   r       radius
 *
 * @return  void
 */
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r)
{
    int a, b;
    int di;
    a = 0;
    b = r;
    di = 3 - (r << 1);
    while (a <= b)
    {
        lcd_draw_point(x0 - b, y0 - a);
        lcd_draw_point(x0 + b, y0 - a);
        lcd_draw_point(x0 - a, y0 + b);
        lcd_draw_point(x0 - b, y0 - a);
        lcd_draw_point(x0 - a, y0 - b);
        lcd_draw_point(x0 + b, y0 + a);
        lcd_draw_point(x0 + a, y0 - b);
        lcd_draw_point(x0 + a, y0 + b);
        lcd_draw_point(x0 - b, y0 + a);
        a++;
        //Bresenham
        if (di < 0)di += 4 * a + 6;
        else
        {
            di += 10 + 4 * (a - b);
            b--;
        }
        lcd_draw_point(x0 + a, y0 + b);
    }
}

static void lcd_show_char(uint16_t x, uint16_t y, uint8_t data, uint32_t size)
{
    uint8_t temp;
    uint8_t num = 0;
    uint8_t pos, t;
    uint16_t colortemp = FORE_COLOR;
    uint8_t *font_buf = NULL;

    if (x > LCD_W - size / 2 || y > LCD_H - size)return;

    data = data - ' ';
#ifdef ASC2_1608
    if (size == 16)
    {
        lcd_address_set(x, y, x + size / 2 - 1, y + size - 1);//(x,y,x+8-1,y+16-1)

        font_buf = (uint8_t *)malloc(size * size);
        if (!font_buf)
        {
            /* fast show char */
            for (pos = 0; pos < size * (size / 2) / 8; pos++)
            {
                temp = asc2_1608[(uint16_t)data * size * (size / 2) / 8 + pos];
                for (t = 0; t < 8; t++)
                {
                    if (temp & 0x80)colortemp = FORE_COLOR;
                    else colortemp = BACK_COLOR;
                    lcd_write_half_word(colortemp);
                    temp <<= 1;
                }
            }
        }
        else
        {
            for (pos = 0; pos < size * (size / 2) / 8; pos++)
            {
                temp = asc2_1608[(uint16_t)data * size * (size / 2) / 8 + pos];
                for (t = 0; t < 8; t++)
                {
                    if (temp & 0x80)colortemp = FORE_COLOR;
                    else colortemp = BACK_COLOR;
                    font_buf[2 * (8 * pos + t)] = colortemp >> 8;
                    font_buf[2 * (8 * pos + t) + 1] = colortemp & 0xFF;
                    temp <<= 1;
                }
            }
            lcd_write_data_buffer(font_buf, size * size);
            free(font_buf);
        }
    }
    else
#endif

#ifdef ASC2_2412
        if (size == 24)
        {
            lcd_address_set(x, y, x + size / 2 - 1, y + size - 1);

            font_buf = (uint8_t *)malloc(size * size);
            if (!font_buf)
            {
                /* fast show char */
                for (pos = 0; pos < (size * 16) / 8; pos++)
                {
                    temp = asc2_2412[(uint16_t)data * (size * 16) / 8 + pos];
                    if (pos % 2 == 0)
                    {
                        num = 8;
                    }
                    else
                    {
                        num = 4;
                    }

                    for (t = 0; t < num; t++)
                    {
                        if (temp & 0x80)colortemp = FORE_COLOR;
                        else colortemp = BACK_COLOR;
                        lcd_write_half_word(colortemp);
                        temp <<= 1;
                    }
                }
            }
            else
            {
                for (pos = 0; pos < (size * 16) / 8; pos++)
                {
                    temp = asc2_2412[(uint16_t)data * (size * 16) / 8 + pos];
                    if (pos % 2 == 0)
                    {
                        num = 8;
                    }
                    else
                    {
                        num = 4;
                    }

                    for (t = 0; t < num; t++)
                    {
                        if (temp & 0x80)colortemp = FORE_COLOR;
                        else colortemp = BACK_COLOR;
                        if (num == 8)
                        {
                            font_buf[2 * (12 * (pos / 2) + t)] = colortemp >> 8;
                            font_buf[2 * (12 * (pos / 2) + t) + 1] = colortemp & 0xFF;
                        }
                        else
                        {
                            font_buf[2 * (8 + 12 * (pos / 2) + t)] = colortemp >> 8;
                            font_buf[2 * (8 + 12 * (pos / 2) + t) + 1] = colortemp & 0xFF;
                        }
                        temp <<= 1;
                    }
                }
                lcd_write_data_buffer(font_buf, size * size);
                free(font_buf);
            }
        }
        else
#endif

#ifdef ASC2_3216
            if (size == 32)
            {
                lcd_address_set(x, y, x + size / 2 - 1, y + size - 1);

                font_buf = (uint8_t *)malloc(size * size);
                if (!font_buf)
                {
                    /* fast show char */
                    for (pos = 0; pos < size * (size / 2) / 8; pos++)
                    {
                        temp = asc2_3216[(uint16_t)data * size * (size / 2) / 8 + pos];
                        for (t = 0; t < 8; t++)
                        {
                            if (temp & 0x80)colortemp = FORE_COLOR;
                            else colortemp = BACK_COLOR;
                            lcd_write_half_word(colortemp);
                            temp <<= 1;
                        }
                    }
                }
                else
                {
                    for (pos = 0; pos < size * (size / 2) / 8; pos++)
                    {
                        temp = asc2_3216[(uint16_t)data * size * (size / 2) / 8 + pos];
                        for (t = 0; t < 8; t++)
                        {
                            if (temp & 0x80)colortemp = FORE_COLOR;
                            else colortemp = BACK_COLOR;
                            font_buf[2 * (8 * pos + t)] = colortemp >> 8;
                            font_buf[2 * (8 * pos + t) + 1] = colortemp & 0xFF;
                            temp <<= 1;
                        }
                    }
                    lcd_write_data_buffer(font_buf, size * size);
                    free(font_buf);
                }
            }
            else
#endif
            {
                LOG_E("There is no any define ASC2_1208 && ASC2_2412 && ASC2_2416 && ASC2_3216 !");
            }
}

/**
 * display the number on the lcd.
 *
 * @param   x       x position
 * @param   y       y position
 * @param   num     number
 * @param   len     length of number
 * @param   size    size of font
 *
 * @return  void
 */
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint32_t size)
{
    lcd_show_string(x, y, size, "%d", num);
}

/**
 * display the string on the lcd.
 *
 * @param   x       x position
 * @param   y       y position
 * @param   size    size of font
 * @param   p       the string to be display
 *
 * @return   0: display success
 * -1: size of font is not support
 */
int lcd_show_string(uint16_t x, uint16_t y, uint32_t size, const char *fmt, ...)
{
#define LCD_STRING_BUF_LEN 128

    va_list args;
    uint8_t buf[LCD_STRING_BUF_LEN] = {0};
    uint8_t *p = NULL; // Replaced RT_NULL

    if (size != 16 && size != 24 && size != 32)
    {
        LOG_E("font size(%d) is not support!", size);
        return -1; // Replaced -RT_ERROR
    }

    va_start(args, fmt);
    vsnprintf((char *)buf, 100, (const char *)fmt, args);
    va_end(args);

    p = buf;
    while (*p != '\0')
    {
        if (x > LCD_W - size / 2)
        {
            x = 0;
            y += size;
        }
        if (y > LCD_H - size)
        {
            y = x = 0;
            lcd_clear(0xF800);
        }
        lcd_show_char(x, y, *p, size);
        x += size / 2;
        p++;
    }

    return 0; // Replaced RT_EOK
}

/**
 * display the image on the lcd.
 *
 * @param   x       x position
 * @param   y       y position
 * @param   length  length of image
 * @param   wide    wide of image
 * @param   p       image
 *
 * @return   0: display success
 * -1: the image is too large
 */
int lcd_show_image(uint16_t x, uint16_t y, uint16_t length, uint16_t wide, const uint8_t *p)
{
    if (!p) return -1;

    if (x + length > LCD_W || y + wide > LCD_H)
    {
        return -1; // Replaced -RT_ERROR
    }

    lcd_address_set(x, y, x + length - 1, y + wide - 1);

    lcd_write_data_buffer(p, length * wide * 2);

    return 0; // Replaced RT_EOK
}

#ifdef PKG_USING_QRCODE
QRCode qrcode;

static uint8_t get_enlargement_factor(uint16_t x, uint16_t y, uint8_t size)
{
    uint8_t enlargement_factor = 1 ;

    if (x + size * 8 <= LCD_W && y + size * 8 <= LCD_H)
    {
        enlargement_factor = 8;
    }
    else if (x + size * 4 <= LCD_W &&y + size * 4 <= LCD_H)
    {
        enlargement_factor = 4;
    }
    else if (x + size * 2 <= LCD_W && y + size * 2 <= LCD_H)
    {
        enlargement_factor = 2;
    }

    return enlargement_factor;
}

static void show_qrcode_by_point(uint16_t x, uint16_t y, uint8_t size, uint8_t enlargement_factor)
{
    uint32_t width = 0, high = 0;
    for (high = 0; high < size; high++)
    {
        for (width = 0; width < size; width++)
        {
            if (qrcode_getModule(&qrcode, width, high))
            {
                /* magnify pixel */
                for (uint32_t offset_y = 0; offset_y < enlargement_factor; offset_y++)
                {
                    for (uint32_t offset_x = 0; offset_x < enlargement_factor; offset_x++)
                    {
                        lcd_draw_point(x + enlargement_factor * width + offset_x, y + enlargement_factor * high + offset_y);
                    }
                }
            }
        }
    }
}

static void show_qrcode_by_line(uint16_t x, uint16_t y, uint8_t size, uint8_t enlargement_factor, uint8_t *qrcode_buf)
{
    uint32_t width = 0, high = 0;
    for (high = 0; high < qrcode.size; high++)
    {
        for (width = 0; width < qrcode.size; width++)
        {
            if (qrcode_getModule(&qrcode, width, high))
            {
                /* magnify pixel */
                for (uint32_t offset_y = 0; offset_y < enlargement_factor; offset_y++)
                {
                    for (uint32_t offset_x = 0; offset_x < enlargement_factor; offset_x++)
                    {
                        /* save the information of modules */
                        qrcode_buf[2 * (enlargement_factor * width + offset_x + offset_y * qrcode.size * enlargement_factor)] = FORE_COLOR >> 8;
                        qrcode_buf[2 * (enlargement_factor * width + offset_x + offset_y * qrcode.size * enlargement_factor) + 1] = FORE_COLOR;
                    }
                }
            }
            else
            {
                /* magnify pixel */
                for (uint32_t offset_y = 0; offset_y < enlargement_factor; offset_y++)
                {
                    for (uint32_t offset_x = 0; offset_x < enlargement_factor; offset_x++)
                    {
                        /* save the information of blank */
                        qrcode_buf[2 * (enlargement_factor * width + offset_x + offset_y * qrcode.size * enlargement_factor)] = BACK_COLOR >> 8;
                        qrcode_buf[2 * (enlargement_factor * width + offset_x + offset_y * qrcode.size * enlargement_factor) + 1] = BACK_COLOR;
                    }
                }
            }
        }
        /* display a line of qrcode */
        lcd_show_image(x, y + high * enlargement_factor, qrcode.size * enlargement_factor, enlargement_factor, qrcode_buf);
    }
}

/**
 * display the qrcode on the lcd.
 * size = (4 * version +17) * enlargement
 *
 * @param   x           x position
 * @param   y           y position
 * @param   version     version of qrcode
 * @param   ecc         level of error correction
 * @param   data        string
 * @param   enlargement enlargement_factor
 *
 * @return   0: display success
 * -1: generate qrcode failed
* -5: memory low
 */
int lcd_show_qrcode(uint16_t x, uint16_t y, uint8_t version, uint8_t ecc, const char *data, uint8_t enlargement)
{
    if (!data) return -1; // Replaced RT_ASSERT

    int8_t result = 0;
    uint8_t enlargement_factor = 1;
    uint8_t *qrcode_buf = NULL;

    if (x + version * 4 + 17 > LCD_W || y + version * 4 + 17 > LCD_H)
    {
        LOG_E("The qrcode is too big!");
        return -1;
    }

    uint8_t *qrcodeBytes = (uint8_t *)calloc(1, qrcode_getBufferSize(version)); // Replaced rt_calloc
    if (qrcodeBytes == NULL)
    {
        LOG_E("no memory for qrcode!");
        return -2; // ENOMEM
    }

    /* generate qrcode */
    result = qrcode_initText(&qrcode, qrcodeBytes, version, ecc, data);
    if (result >= 0)
    {
        /* set enlargement factor */
        if(enlargement == 0)
        {
            enlargement_factor = get_enlargement_factor(x, y, qrcode.size);
        }
        else
        {
            enlargement_factor = enlargement;
        }
        
        /* malloc memory for quick display of qrcode */
        qrcode_buf = malloc(qrcode.size * 2 * enlargement_factor * enlargement_factor); // Replaced rt_malloc
        if (qrcode_buf == NULL)
        {
            /* clear lcd */
            lcd_fill(x, y, x + qrcode.size, y + qrcode.size, BACK_COLOR);

            /* draw point to display qrcode */
            show_qrcode_by_point(x, y, qrcode.size, enlargement_factor);
        }
        else
        {
            /* quick display of qrcode */
            show_qrcode_by_line(x, y, qrcode.size, enlargement_factor,qrcode_buf);
        }
        result = 0; // EOK
    }
    else
    {
        LOG_E("QRCODE(%s) generate falied(%d)\n", data, result); // Fixed original 'qrstr' typo to 'data'
        result = -2; // ENOMEM
        goto __exit;
    }

__exit:
    if (qrcodeBytes)
    {
        free(qrcodeBytes); // Replaced rt_free
    }

    if (qrcode_buf)
    {
        free(qrcode_buf); // Replaced rt_free
    }

    return result;
}
#endif

void lcd_fill_array(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, void *pcolor)
{
    uint16_t *pixel = NULL;
    uint16_t cycle_y, x_offset = 0;

    pixel = (uint16_t *)pcolor;

    lcd_address_set(x_start, y_start, x_end, y_end);
    for (cycle_y = y_start; cycle_y <= y_end;)
    {
        for (x_offset = 0; x_start + x_offset <= x_end; x_offset++)
        {
            LCD->_u8_RAM = (*pixel)>>8;
            LCD->_u8_RAM = *pixel++;
        }
        cycle_y++;
    }
}

void LCD_DrawLine(const char *pixel, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    uint16_t t;
    int xerr = 0, yerr = 0, delta_x, delta_y, distance;
    int incx, incy, uRow, uCol;
    delta_x = x2 - x1; // 计算坐标增量
    delta_y = y2 - y1;
    uRow = x1;
    uCol = y1;

    if (delta_x > 0)
        incx = 1; // 设置单步方向
    else if (delta_x == 0)
        incx = 0; // 垂直线
    else
    {
        incx = -1;
        delta_x = -delta_x;
    }

    if (delta_y > 0)
        incy = 1;
    else if (delta_y == 0)
        incy = 0; // 水平线
    else
    {
        incy = -1;
        delta_y = -delta_y;
    }

    if (delta_x > delta_y)
        distance = delta_x; // 选取基本增量坐标轴
    else
        distance = delta_y;

    for (t = 0; t <= distance + 1; t++) // 画线输出
    {
        // LCD_DrawPoint(uRow, uCol); //画点
        LCD_Fast_DrawPoint(pixel, uRow, uCol);
        xerr += delta_x;
        yerr += delta_y;

        if (xerr > distance)
        {
            xerr -= distance;
            uRow += incx;
        }

        if (yerr > distance)
        {
            yerr -= distance;
            uCol += incy;
        }
    }
}
void LCD_HLine(const char *pixel, int x1, int x2, int y)
{
    LCD_DrawLine(pixel, x1, y, x2, y);
}

void LCD_VLine(const char *pixel, int x, int y1, int y2)
{
    LCD_DrawLine(pixel, x, y1, x, y2);
}

void LCD_BlitLine(const char *pixel, int x, int y, size_t size)
{
    LCD_SetCursor(x, y);
    LCD_WriteRAM_Prepare();
    uint16_t *p = (uint16_t *)pixel;
    for (; size > 0; size--, p++)
    {
        // Ensure 16-bit split writing for 8-bit bus compatibility
        LCD_WR_DATA16(*p);
    }
}

#ifdef BSP_USING_ONBOARD_LCD_TEAREFFECT
__weak void lcd_teareffect_isr()   // 60hz
{

}
#endif /* BSP_USING_ONBOARD_LCD_TEAREFFECT */

// Removed struct rt_device from signature
int drv_lcd_init(void)
{

    SRAM_HandleTypeDef hsram1 = {0};
    FSMC_NORSRAM_TimingTypeDef read_timing = {0};
    FSMC_NORSRAM_TimingTypeDef write_timing = {0};

    // Note: Pin Modes (like input/output configuration) are typically
    // handled in the MX_GPIO_Init() generated by CubeMX.

#ifdef BSP_USING_ONBOARD_LCD_TEAREFFECT
    // External Interrupt configuration should also be handled by CubeMX / NVIC
#endif /* BSP_USING_ONBOARD_LCD_TEAREFFECT */

    // Hardware Reset Sequence using HAL Delay
    HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(100);

    // FSMC_NORSRAM_TimingTypeDef Timing = {0};

    /** Perform the SRAM1 memory initialization sequence
     */
    hsram1.Instance = FSMC_NORSRAM_DEVICE;
    hsram1.Extended = FSMC_NORSRAM_EXTENDED_DEVICE;
    /* hsram1.Init */
    hsram1.Init.NSBank = FSMC_NORSRAM_BANK3;
    hsram1.Init.DataAddressMux = FSMC_DATA_ADDRESS_MUX_DISABLE;
    hsram1.Init.MemoryType = FSMC_MEMORY_TYPE_SRAM;
    hsram1.Init.MemoryDataWidth = FSMC_NORSRAM_MEM_BUS_WIDTH_8;
    hsram1.Init.BurstAccessMode = FSMC_BURST_ACCESS_MODE_DISABLE;
    hsram1.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW;
    hsram1.Init.WrapMode = FSMC_WRAP_MODE_DISABLE;
    hsram1.Init.WaitSignalActive = FSMC_WAIT_TIMING_BEFORE_WS;
    hsram1.Init.WriteOperation = FSMC_WRITE_OPERATION_ENABLE;
    hsram1.Init.WaitSignal = FSMC_WAIT_SIGNAL_DISABLE;
    hsram1.Init.ExtendedMode = FSMC_EXTENDED_MODE_ENABLE;
    hsram1.Init.AsynchronousWait = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    hsram1.Init.WriteBurst = FSMC_WRITE_BURST_DISABLE;
    hsram1.Init.PageSize = FSMC_PAGE_SIZE_NONE;
    // /* Timing */

    read_timing.AddressSetupTime = 0XF;	 //地址建立时间（ADDSET）为16个HCLK 1/168M=6ns*16=96ns	
    read_timing.AddressHoldTime = 0x00;	 //地址保持时间（ADDHLD）模式A未用到	
    read_timing.DataSetupTime = 60;			//数据保存时间为60个HCLK	=6*60=360ns
    read_timing.BusTurnAroundDuration = 0x00;
    read_timing.CLKDivision = 0x00;
    read_timing.DataLatency = 0x00;
    read_timing.AccessMode = FSMC_ACCESS_MODE_A;	 //模式A 


    write_timing.AddressSetupTime =9;	      //地址建立时间（ADDSET）为9个HCLK =54ns 
    write_timing.AddressHoldTime = 0x00;	 //地址保持时间（A		
    write_timing.DataSetupTime = 8;		 //数据保存时间为6ns*9个HCLK=54ns
    write_timing.BusTurnAroundDuration = 0x00;
    write_timing.CLKDivision = 0x00;
    write_timing.DataLatency = 0x00;
    write_timing.AccessMode = FSMC_ACCESS_MODE_A;	 //模式A 

    if (HAL_SRAM_Init(&hsram1, &read_timing, &write_timing) != HAL_OK)
    {
        Error_Handler( );
    }

    HAL_Delay(100);

    // 尝试st7789v3 ID的读取
    LCD_WR_REG(0X04);
    lcddev.id = LCD_RD_DATA8(); // dummy read
    lcddev.id = LCD_RD_DATA8(); // ID2
    lcddev.id = LCD_RD_DATA8(); // ID3
    lcddev.id <<= 8;
    lcddev.id |= LCD_RD_DATA8();

    LOG_I(" LCD ID:%x", lcddev.id); // 打印LCD ID

    //************* Start Initial Sequence **********//
    /* Memory Data Access Control */
    LCD_WR_REG(0x36);
    LCD_WR_DATA8(0x00);
    /* RGB 5-6-5-bit  */
    LCD_WR_REG(0x3A); 
    LCD_WR_DATA8(0x65);
    /* Porch Setting */
    LCD_WR_REG(0xB2);
    LCD_WR_DATA8(0x0C);
    LCD_WR_DATA8(0x0C);
    LCD_WR_DATA8(0x00);
    LCD_WR_DATA8(0x33);
    LCD_WR_DATA8(0x33); 
    /* Gate Control */
    LCD_WR_REG(0xB7); 
    LCD_WR_DATA8(0x35);  
    /* VCOM Setting */
    LCD_WR_REG(0xBB);
    LCD_WR_DATA8(0x37);
    /* LCM Control */
    LCD_WR_REG(0xC0);
    LCD_WR_DATA8(0x2C);
    /* VDV and VRH Command Enable */
    LCD_WR_REG(0xC2);
    LCD_WR_DATA8(0x01);
    /* VRH Set */
    LCD_WR_REG(0xC3);
        LCD_WR_DATA8(0x12);
        /* VDV Set */
        LCD_WR_REG(0xC4);
        LCD_WR_DATA8(0x20);
        /* Frame Rate Control in Normal Mode */
        LCD_WR_REG(0xC6);
        LCD_WR_DATA8(0x0F);
        /* Power Control 1 */
        LCD_WR_REG(0xD0);
        LCD_WR_DATA8(0xA4);
        LCD_WR_DATA8(0xA1);
        /* Positive Voltage Gamma Control */
        LCD_WR_REG(0xE0);
        LCD_WR_DATA8(0xD0);
        LCD_WR_DATA8(0x04);
        LCD_WR_DATA8(0x0D);
        LCD_WR_DATA8(0x11);
        LCD_WR_DATA8(0x13);
        LCD_WR_DATA8(0x2B);
        LCD_WR_DATA8(0x3F);
        LCD_WR_DATA8(0x54);
        LCD_WR_DATA8(0x4C);
        LCD_WR_DATA8(0x18);
        LCD_WR_DATA8(0x0D);
        LCD_WR_DATA8(0x0B);
        LCD_WR_DATA8(0x1F);
        LCD_WR_DATA8(0x23);
        /* Negative Voltage Gamma Control */
        LCD_WR_REG(0xE1);
        LCD_WR_DATA8(0xD0);
        LCD_WR_DATA8(0x04);
        LCD_WR_DATA8(0x0C);
        LCD_WR_DATA8(0x11);
        LCD_WR_DATA8(0x13);
        LCD_WR_DATA8(0x2C);
        LCD_WR_DATA8(0x3F);
        LCD_WR_DATA8(0x44);
        LCD_WR_DATA8(0x51);
        LCD_WR_DATA8(0x2F);
        LCD_WR_DATA8(0x1F);
        LCD_WR_DATA8(0x1F);
        LCD_WR_DATA8(0x20);
        LCD_WR_DATA8(0x23);
        /* Display Inversion On */
        LCD_WR_REG(0x21);       // Turn on color inversion
    #ifdef BSP_USING_ONBOARD_LCD_TEAREFFECT
        /* TearEffect Sync On */
        LCD_WR_REG(0x35);
        LCD_WR_DATA8(0x00);
    #endif /* BSP_USING_ONBOARD_LCD_TEAREFFECT */

        /* Sleep Out */
        LCD_WR_REG(0x11);

        HAL_Delay(120); // Replaced rt_thread_mdelay

        /* display on */
        LCD_WR_REG(0x29);

        // Reconfigure FSMC timing registers for maximum speed
        {
            FSMC_Bank1E->BWTR[6] &= ~(0XF << 0); // Clear Address setup time (ADDSET)
            FSMC_Bank1E->BWTR[6] &= ~(0XF << 8); // Clear Data setup time
            FSMC_Bank1E->BWTR[6] |= 3 << 0;      // ADDSET = 3 HCLK = 18ns
            FSMC_Bank1E->BWTR[6] |= 2 << 8;      // DATAST = 6ns * 3 HCLK = 18ns
        }

        LCD_Display_Dir(0); // Default to portrait
        lcd_clear(BACK_COLOR);

        // Turn on backlight (using bare-metal function defined earlier)
        LCD_PWM_BackLightInit();
        LCD_BackLightSet(80);

        return 0; // Replaced RT_EOK
    }

    // ---------------------------------------------------------
    // Bare-Metal Initialization Wrapper
    // ---------------------------------------------------------
    int drv_lcd_hw_init(void)
    {
        // Simplified hardware initialization entry point
        _lcd.bits_per_pixel = 16;
        _lcd.pixel_format = 2; // RGB565 equivalent

        // Run the actual hardware sequence
        drv_lcd_init();

        return 0;
    }

    #ifdef BSP_USING_ONBOARD_LCD_TEST
    // Bare-metal test function (Removed RTOS thread creation and msh console exports)
    void lcd_auto_fill(uint32_t num)
    {
        do
        {
            // Use HAL_GetTick() instead of rt_tick_get() to generate random colors
            lcd_clear((uint16_t)(HAL_GetTick() % 65535));
            HAL_Delay(500); // Replaced rt_thread_mdelay
        } while (--num);
    }
    #endif
