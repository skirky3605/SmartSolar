#include "I2C.h"
#include "Delay.h"

static void PCA9685_SDA_IN(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = PCA9685_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PCA9685_I2C_PORT, &GPIO_InitStructure);
}

static void PCA9685_SDA_OUT(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = PCA9685_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PCA9685_I2C_PORT, &GPIO_InitStructure);
}

void PCA9685_IIC_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin = PCA9685_SCL_PIN | PCA9685_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PCA9685_I2C_PORT, &GPIO_InitStructure);

    PCA9685_IIC_SCL_1;
    PCA9685_IIC_SDA_1;
}

void PCA9685_IIC_Start(void)
{
    PCA9685_SDA_OUT();
    PCA9685_IIC_SDA_1;
    PCA9685_IIC_SCL_1;
    Delay_us(4);
    PCA9685_IIC_SDA_0;
    Delay_us(4);
    PCA9685_IIC_SCL_0;
}

void PCA9685_IIC_Stop(void)
{
    PCA9685_SDA_OUT();
    PCA9685_IIC_SCL_0;
    PCA9685_IIC_SDA_0;
    Delay_us(4);
    PCA9685_IIC_SCL_1;
    PCA9685_IIC_SDA_1;
    Delay_us(4);
}

uint8_t PCA9685_IIC_Wait_Ack(void)
{
    uint8_t ucErrTime = 0;
    PCA9685_SDA_IN();
    PCA9685_IIC_SDA_1;
    Delay_us(1);
    PCA9685_IIC_SCL_1;
    Delay_us(1);
    while (PCA9685_READ_SDA)
    {
        ucErrTime++;
        if (ucErrTime > 250)
        {
            PCA9685_IIC_Stop();
            return 1;
        }
    }
    PCA9685_IIC_SCL_0;
    return 0;
}

void PCA9685_IIC_Send_Byte(uint8_t txd)
{
    uint8_t t;
    PCA9685_SDA_OUT();
    PCA9685_IIC_SCL_0;
    for (t = 0; t < 8; t++)
    {
        if ((txd & 0x80) >> 7)
        {
            PCA9685_IIC_SDA_1;
        }
        else
        {
            PCA9685_IIC_SDA_0;
        }
        txd <<= 1;
        Delay_us(2);
        PCA9685_IIC_SCL_1;
        Delay_us(2);
        PCA9685_IIC_SCL_0;
        Delay_us(2);
    }
}
