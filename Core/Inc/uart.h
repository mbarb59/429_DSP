#ifndef UART_H
#define UART_H

#include <stdint.h>

void UART_Init(void);
void UART_SendChar(char c);
void UART_SendString(const char *s);
void UART_Flush(void);
void UART_SendHex8(uint8_t value);
void UART_SendHex32(uint32_t value);
void UART_SendDec(uint32_t value);
int UART_CharAvailable(void);
char UART_ReadChar(void);

#endif /* UART_H */
