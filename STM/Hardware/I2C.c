#include "I2C.h"
#include "Delay.h"

/* Open-drain lines require pull-ups to the logic supply on the module. */
void PCA9685_IIC_Stop(void)
{
    PCA9685_IIC_SCL_0;
    PCA9685_IIC_SDA_0;
    Delay_us(5);
    PCA9685_IIC_SCL_1;
    Delay_us(5);
    PCA9685_IIC_SDA_1;
    Delay_us(5);
}

void PCA9685_IIC_Init(void)
{
    GPIO_InitTypeDef gpio;
    uint8_t i;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_SetBits(PCA9685_I2C_PORT, PCA9685_SCL_PIN | PCA9685_SDA_PIN);
    gpio.GPIO_Pin = PCA9685_SCL_PIN | PCA9685_SDA_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_OD;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(PCA9685_I2C_PORT, &gpio);
    Delay_ms(50);

    /* Finish any byte interrupted by an MCU reset, then send STOP. */
    for (i = 0; i < 9; i++)
    {
        PCA9685_IIC_SCL_0;
        Delay_us(5);
        PCA9685_IIC_SCL_1;
        Delay_us(5);
    }
    PCA9685_IIC_Stop();
}

void PCA9685_IIC_Start(void)
{
    PCA9685_IIC_SCL_0;
    PCA9685_IIC_SDA_1;
    Delay_us(5);
    PCA9685_IIC_SCL_1;
    Delay_us(5);
    PCA9685_IIC_SDA_0;
    Delay_us(5);
    PCA9685_IIC_SCL_0;
}

uint8_t PCA9685_IIC_Wait_Ack(void)
{
    uint8_t nack;
    PCA9685_IIC_SCL_0;
    PCA9685_IIC_SDA_1; /* Release SDA for the slave to acknowledge. */
    Delay_us(5);
    PCA9685_IIC_SCL_1;
    Delay_us(5);
    nack = PCA9685_READ_SDA ? 1U : 0U;
    PCA9685_IIC_SCL_0;
    Delay_us(5);
    if (nack) PCA9685_IIC_Stop();
    return nack;
}

void PCA9685_IIC_Send_Byte(uint8_t txd)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        PCA9685_IIC_SCL_0;
        if (txd & 0x80U) { PCA9685_IIC_SDA_1; }
        else { PCA9685_IIC_SDA_0; }
        Delay_us(5);
        PCA9685_IIC_SCL_1;
        Delay_us(5);
        PCA9685_IIC_SCL_0;
        txd <<= 1;
    }
}
