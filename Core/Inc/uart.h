#ifndef UART_H
#define UART_H

#include <stdint.h>

void UART_Init(void);
void UART_SendChar(char c);
void UART_SendString(const char *str);

#endif