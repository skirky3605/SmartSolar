#include "BH1750_I2C.h"
#include "Delay.h"

/* 获取对应通道的 GPIO 端口与引脚 */
static GPIO_TypeDef* Get_SDA_Port(uint8_t id)
{
    switch (id)
    {
        case 1: return BH1750_SDA1_PORT;
        case 2: return BH1750_SDA2_PORT;
        case 3: return BH1750_SDA3_PORT;
        case 4: return BH1750_SDA4_PORT;
        default: return BH1750_SDA1_PORT;
    }
}

static uint16_t Get_SDA_Pin(uint8_t id)
{
    switch (id)
    {
        case 1: return BH1750_SDA1_PIN;
        case 2: return BH1750_SDA2_PIN;
        case 3: return BH1750_SDA3_PIN;
        case 4: return BH1750_SDA4_PIN;
        default: return BH1750_SDA1_PIN;
    }
}

static void SDA_IN(uint8_t id)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_TypeDef *port = Get_SDA_Port(id);
    uint16_t pin = Get_SDA_Pin(id);

    GPIO_InitStructure.GPIO_Pin = pin;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(port, &GPIO_InitStructure);
}

static void SDA_OUT(uint8_t id)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_TypeDef *port = Get_SDA_Port(id);
    uint16_t pin = Get_SDA_Pin(id);

    GPIO_InitStructure.GPIO_Pin = pin;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(port, &GPIO_InitStructure);
}

static void SET_SDA(uint8_t id, uint8_t val)
{
    GPIO_TypeDef *port = Get_SDA_Port(id);
    uint16_t pin = Get_SDA_Pin(id);
    GPIO_WriteBit(port, pin, (BitAction)val);
}

static uint8_t READ_SDA(uint8_t id)
{
    GPIO_TypeDef *port = Get_SDA_Port(id);
    uint16_t pin = Get_SDA_Pin(id);
    return GPIO_ReadInputDataBit(port, pin);
}

/**
  * @brief  初始化 4 路 BH1750 的 GPIO 引脚
  */
void BH1750_IIC_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    // 开启 GPIOA 和 GPIOB 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);

    // 1. 初始化共用 SCL 引脚 (PB11)
    GPIO_InitStructure.GPIO_Pin = BH1750_SCL_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BH1750_SCL_PORT, &GPIO_InitStructure);

    // 2. 初始化 4 路 SDA 引脚 (PB10, PA4, PA5, PA6)
    GPIO_InitStructure.GPIO_Pin = BH1750_SDA1_PIN;
    GPIO_Init(BH1750_SDA1_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = BH1750_SDA2_PIN | BH1750_SDA3_PIN | BH1750_SDA4_PIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 默认输出高电平（I2C 空闲状态）
    BH1750_SCL_1();
    SET_SDA(1, 1);
    SET_SDA(2, 1);
    SET_SDA(3, 1);
    SET_SDA(4, 1);
}

void BH1750_IIC_Start(uint8_t id)
{
    SDA_OUT(id);
    SET_SDA(id, 1);
    BH1750_SCL_1();
    Delay_us(4);
    SET_SDA(id, 0);
    Delay_us(4);
    BH1750_SCL_0();
}

void BH1750_IIC_Stop(uint8_t id)
{
    SDA_OUT(id);
    BH1750_SCL_0();
    SET_SDA(id, 0);
    Delay_us(4);
    BH1750_SCL_1();
    SET_SDA(id, 1);
    Delay_us(4);
}

uint8_t BH1750_IIC_Wait_Ack(uint8_t id)
{
    uint8_t ucErrTime = 0;
    SDA_IN(id);
    SET_SDA(id, 1);
    Delay_us(2);
    BH1750_SCL_1();
    Delay_us(2);
    while (READ_SDA(id))
    {
        ucErrTime++;
        if (ucErrTime > 250)
        {
            BH1750_IIC_Stop(id);
            return 1;
        }
    }
    BH1750_SCL_0();
    return 0;
}

static void BH1750_IIC_Ack(uint8_t id)
{
    SDA_OUT(id);
    BH1750_SCL_0();
    SET_SDA(id, 0);
    Delay_us(2);
    BH1750_SCL_1();
    Delay_us(2);
    BH1750_SCL_0();
}

static void BH1750_IIC_NAck(uint8_t id)
{
    SDA_OUT(id);
    BH1750_SCL_0();
    SET_SDA(id, 1);
    Delay_us(2);
    BH1750_SCL_1();
    Delay_us(2);
    BH1750_SCL_0();
}

void BH1750_IIC_Send_Byte(uint8_t id, uint8_t txd)
{
    uint8_t t;
    SDA_OUT(id);
    BH1750_SCL_0();
    for (t = 0; t < 8; t++)
    {
        if ((txd & 0x80) >> 7)
        {
            SET_SDA(id, 1);
        }
        else
        {
            SET_SDA(id, 0);
        }
        txd <<= 1;
        Delay_us(2);
        BH1750_SCL_1();
        Delay_us(2);
        BH1750_SCL_0();
        Delay_us(2);
    }
}

uint8_t BH1750_IIC_Read_Byte(uint8_t id, uint8_t ack)
{
    uint8_t i;
    uint8_t receive = 0;
    SDA_IN(id);
    for (i = 0; i < 8; i++)
    {
        BH1750_SCL_0();
        Delay_us(2);
        BH1750_SCL_1();
        receive <<= 1;
        if (READ_SDA(id)) receive++;
        Delay_us(1);
    }
    if (!ack)
    {
        BH1750_IIC_NAck(id);
    }
    else
    {
        BH1750_IIC_Ack(id);
    }
    return receive;
}
