#ifndef __BH1750_I2C_H
#define __BH1750_I2C_H

#include "stm32f10x.h"

/* 
 * 4路 BH1750 光照传感器引脚配置：
 * SCL 采用共用时钟线；4路分别拥有独立的 SDA 数据线。
 * 所有 BH1750 的 ADDR 引脚均接 GND（默认 I2C 地址 0x23）。
 */

// SCL 共用时钟引脚
#define BH1750_SCL_PORT        GPIOB
#define BH1750_SCL_PIN         GPIO_Pin_11

// 4路 SDA 引脚定义
#define BH1750_SDA1_PORT       GPIOB
#define BH1750_SDA1_PIN        GPIO_Pin_10   // 传感器 1 (例如: 左上 / Top-Left)

#define BH1750_SDA2_PORT       GPIOA
#define BH1750_SDA2_PIN        GPIO_Pin_4    // 传感器 2 (例如: 右上 / Top-Right)

#define BH1750_SDA3_PORT       GPIOA
#define BH1750_SDA3_PIN        GPIO_Pin_5    // 传感器 3 (例如: 左下 / Bottom-Left)

#define BH1750_SDA4_PORT       GPIOA
#define BH1750_SDA4_PIN        GPIO_Pin_6    // 传感器 4 (例如: 右下 / Bottom-Right)

/* SCL 操作宏 */
#define BH1750_SCL_1()         GPIO_WriteBit(BH1750_SCL_PORT, BH1750_SCL_PIN, Bit_SET)
#define BH1750_SCL_0()         GPIO_WriteBit(BH1750_SCL_PORT, BH1750_SCL_PIN, Bit_RESET)

/* 底层接口函数 */
void BH1750_IIC_Init(void);
void BH1750_IIC_Start(uint8_t id);
void BH1750_IIC_Stop(uint8_t id);
void BH1750_IIC_Send_Byte(uint8_t id, uint8_t txd);
uint8_t BH1750_IIC_Read_Byte(uint8_t id, uint8_t ack);
uint8_t BH1750_IIC_Wait_Ack(uint8_t id);

#endif
