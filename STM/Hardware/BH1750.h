#ifndef __BH1750_H
#define __BH1750_H

#include "stm32f10x.h"

/* BH1750 I2C地址 (ADDR引脚接地为0x23) */
#define BH1750_ADDR_WRITE  0x46  // 0x23 << 1 | 0
#define BH1750_ADDR_READ   0x47  // 0x23 << 1 | 1

/* BH1750 指令集 */
#define BH1750_POWER_DOWN  0x00  // 断电
#define BH1750_POWER_ON    0x01  // 通电
#define BH1750_RESET       0x07  // 复位寄存器
#define BH1750_CONT_H_RES  0x10  // 连续高分辨率模式 (1 lx)

/* 函数声明 */
void BH1750_Init(void);
uint16_t BH1750_Read_Lux(uint8_t id);
void BH1750_Read_All(uint16_t lux[4]);

#endif
