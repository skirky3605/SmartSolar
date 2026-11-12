#ifndef __USART1_H
#define __USART1_H

#include "stm32f10x.h"
#include <stdio.h>

#define USART1_RX_BUF_SIZE 128

extern uint8_t  USART1_RxBuffer[USART1_RX_BUF_SIZE];
extern volatile uint16_t USART1_RxLen;
extern volatile uint8_t  USART1_RxFlag;

void USART1_Init(uint32_t baudrate);
void USART1_SendByte(uint8_t byte);
void USART1_SendString(char *str);
void USART1_SendBuffer(uint8_t *buf, uint16_t len);
void USART1_Printf(char *format, ...);
void USART1_ClearRx(void);

#endif
