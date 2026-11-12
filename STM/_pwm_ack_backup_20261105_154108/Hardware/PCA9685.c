#include "stm32f10x.h"                  // Device header
#include "PCA9685.h"
#include "I2C.h"
#include "Delay.h"

#define PCA9685_ADDR 0x40                 // PCA9685 I2C地址 (默认A0~A5全接地为0x40)

/**
  * @brief          写入PCA9685寄存器
  * @param  Reg     寄存器地址
  * @param  Data    要写入的数据
  * @retval         无
  */
void PCA9685_WriteReg(uint8_t Reg, uint8_t Data)
{
    PCA9685_IIC_Start();                              // 发送起始信号

    PCA9685_IIC_Send_Byte(PCA9685_ADDR << 1);         // 写入从机地址 + 写方向(0)
    PCA9685_IIC_Wait_Ack();                           // 等待应答

    PCA9685_IIC_Send_Byte(Reg);                       // 写入寄存器地址
    PCA9685_IIC_Wait_Ack();                           // 等待应答

    PCA9685_IIC_Send_Byte(Data);                      // 写入数据
    PCA9685_IIC_Wait_Ack();                           // 等待应答

    PCA9685_IIC_Stop();                               // 停止信号
}

/**
  * @brief            初始化PCA9865
  * @param  frequency PWM频率 (舵机控制通常传50)
  * @retval           无
  */
void PCA9685_Init(uint16_t frequency)
{
    uint32_t prescale;

    // 先初始化底层 I2C 引脚 (PB13 SCL, PB14 SDA)
    PCA9685_IIC_Init();

    // 限制频率有效范围 (内部时钟 25MHz)
    if (frequency < 24 || frequency > 1526)
    {
        return;
    }

    // prescale = round(25000000 / (4096 * frequency)) - 1
    prescale = (25000000UL + 2048UL * frequency) / (4096UL * frequency) - 1UL;

    PCA9685_WriteReg(PCA9685_MODE1, 0x10); // SLEEP=1，进入休眠模式才能配置PRE_SCALE
    Delay_ms(2);
    PCA9685_WriteReg(PCA9685_PRE_SCALE, (uint8_t)prescale); // 设置预分频
    PCA9685_WriteReg(PCA9685_MODE1, 0x20); // 退出休眠，AI=1 (自动自增)
    Delay_ms(1);                           // 等待内部振荡器稳定
    PCA9685_WriteReg(PCA9685_MODE2, 0x04); // OUTDRV=1 (图腾柱/推挽输出)
}

/**
  * @brief           设置PCA9685的PWM输出
  * @param  Channel  通道号（0-15）
  * @param  On       PWM的ON时间（0-4095）
  * @param  Off      PWM的OFF时间（0-4095）
  * @retval          无
  */
void PCA9685_SetPWM(uint8_t Channel, uint16_t On, uint16_t Off)
{
    uint8_t reg;

    if (Channel > 15 || On > 4095 || Off > 4095) 
    {
        return;                                         // 通道范围检查
    }

    reg = PCA9685_LED0_ON_L + 4 * Channel;

    PCA9685_WriteReg(reg,     On & 0xFF);               // 设置ON低字节
    PCA9685_WriteReg(reg + 1, (On >> 8) & 0x0F);        // 设置ON高字节
    PCA9685_WriteReg(reg + 2, Off & 0xFF);              // 设置OFF低字节
    PCA9685_WriteReg(reg + 3, (Off >> 8) & 0x0F);       // 设置OFF高字节
}

/**
  * @brief           设置指定通道舵机角度 (支持0~180度)
  * @param  Channel  舵机通道 (0~15)
  * @param  Angle    舵机目标角度 (0.0f ~ 180.0f)
  * @retval          无
  */
void PCA9685_SetServoAngle(uint8_t Channel, float Angle)
{
    uint16_t Pulse_us;
    uint32_t Count;

    if (Channel > 15) return;
    if (Angle < 0.0f)   Angle = 0.0f;
    if (Angle > 180.0f) Angle = 180.0f;

    // 标准舵机脉冲：0度对应 500us，180度对应 2500us
    // 50Hz频率下，周期为 20ms = 20000us，对应 4096 个计数阶梯
    Pulse_us = (uint16_t)(500.0f + (Angle * 2000.0f / 180.0f));
    Count = (uint32_t)Pulse_us * 4096UL / 20000UL;

    PCA9685_SetPWM(Channel, 0, (uint16_t)Count);
}
