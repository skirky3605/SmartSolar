#include "BH1750.h"
#include "BH1750_I2C.h"
#include "Delay.h"

/**
  * @brief  初始化 4 路 BH1750 传感器
  */
void BH1750_Init(void)
{
    uint8_t i;

    // 1. 初始化底层的 GPIO
    BH1750_IIC_Init();

    // 2. 依次唤醒并配置 4 个传感器为连续高分辨率模式
    for (i = 1; i <= 4; i++)
    {
        // 通电
        BH1750_IIC_Start(i);
        BH1750_IIC_Send_Byte(i, BH1750_ADDR_WRITE);
        BH1750_IIC_Wait_Ack(i);
        BH1750_IIC_Send_Byte(i, BH1750_POWER_ON);
        BH1750_IIC_Wait_Ack(i);
        BH1750_IIC_Stop(i);

        // 设置为连续高分辨率模式 (1 lx, 约120ms测量周期)
        BH1750_IIC_Start(i);
        BH1750_IIC_Send_Byte(i, BH1750_ADDR_WRITE);
        BH1750_IIC_Wait_Ack(i);
        BH1750_IIC_Send_Byte(i, BH1750_CONT_H_RES);
        BH1750_IIC_Wait_Ack(i);
        BH1750_IIC_Stop(i);
    }

    // 等待首次测量完成
    Delay_ms(180);
}

/**
  * @brief  读取指定传感器通道的光照强度
  * @param  id: 传感器编号 (1 ~ 4)
  * @retval 光照度数值 (单位: Lux)，若读取失败返回 65535
  */
uint16_t BH1750_Read_Lux(uint8_t id)
{
    uint8_t buf[2] = {0};
    uint16_t raw_data = 0;
    float lux = 0.0f;

    if (id < 1 || id > 4) return 65535;

    // 1. 发送读命令
    BH1750_IIC_Start(id);
    BH1750_IIC_Send_Byte(id, BH1750_ADDR_READ);
    if (BH1750_IIC_Wait_Ack(id))
    {
        BH1750_IIC_Stop(id);
        return 65535; // 传感器无应答
    }

    // 2. 读取高8位数据，回复 ACK
    buf[0] = BH1750_IIC_Read_Byte(id, 1);

    // 3. 读取低8位数据，回复 NACK
    buf[1] = BH1750_IIC_Read_Byte(id, 0);

    // 4. 停止信号
    BH1750_IIC_Stop(id);

    // 5. 换算照度值 (Lux = 原始计数值 / 1.2)
    raw_data = (uint16_t)((buf[0] << 8) | buf[1]);
    lux = (float)raw_data / 1.2f;

    return (uint16_t)lux;
}

/**
  * @brief  一次性读取全部 4 个传感器的光照强度
  * @param  lux: 长度至少为 4 的数组，lux[0]~lux[3] 分别存储传感器 1~4 的光照值
  */
void BH1750_Read_All(uint16_t lux[4])
{
    uint8_t i;
    for (i = 0; i < 4; i++)
    {
        lux[i] = BH1750_Read_Lux(i + 1);
    }
}
