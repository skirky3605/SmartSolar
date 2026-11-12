#ifndef __I2C_H
#define __I2C_H

#include "stm32f10x.h"

/* PCA9685 专属软件 I2C 引脚定义: PB13 (SCL), PB14 (SDA) */
#define PCA9685_SCL_PIN     GPIO_Pin_13
#define PCA9685_SDA_PIN     GPIO_Pin_14
#define PCA9685_I2C_PORT    GPIOB

/* 快速操作宏 */
#define PCA9685_IIC_SCL_1   GPIO_WriteBit(PCA9685_I2C_PORT, PCA9685_SCL_PIN, Bit_SET)
#define PCA9685_IIC_SCL_0   GPIO_WriteBit(PCA9685_I2C_PORT, PCA9685_SCL_PIN, Bit_RESET)

#define PCA9685_IIC_SDA_1   GPIO_WriteBit(PCA9685_I2C_PORT, PCA9685_SDA_PIN, Bit_SET)
#define PCA9685_IIC_SDA_0   GPIO_WriteBit(PCA9685_I2C_PORT, PCA9685_SDA_PIN, Bit_RESET)

#define PCA9685_READ_SDA    GPIO_ReadInputDataBit(PCA9685_I2C_PORT, PCA9685_SDA_PIN)

/* 函数声明，避免与 BH1750 等其他 I2C 模块符号冲突 */
void PCA9685_IIC_Init(void);
void PCA9685_IIC_Start(void);
void PCA9685_IIC_Stop(void);
void PCA9685_IIC_Send_Byte(uint8_t txd);
uint8_t PCA9685_IIC_Wait_Ack(void);

#endif
