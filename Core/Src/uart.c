#include "stm32g431xx.h"
#include "uart.h"

void UART_Init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;

    /* PA2 TX */
    GPIOA->MODER &= ~(3UL << (2 * 2));
    GPIOA->MODER |=  (2UL << (2 * 2));

    /* PA3 RX */
    GPIOA->MODER &= ~(3UL << (3 * 2));
    GPIOA->MODER |=  (2UL << (3 * 2));

    /* AF7 = USART2 */
    GPIOA->AFR[0] &= ~(0xFUL << (2 * 4));
    GPIOA->AFR[0] |=  (7UL << (2 * 4));

    GPIOA->AFR[0] &= ~(0xFUL << (3 * 4));
    GPIOA->AFR[0] |=  (7UL << (3 * 4));

    USART2->BRR = 139;

    USART2->CR1 = USART_CR1_TE | USART_CR1_RE;
    USART2->CR1 |= USART_CR1_UE;
}

void UART_SendChar(char c)
{
    while (!(USART2->ISR & USART_ISR_TXE_TXFNF))
    {
    }

    USART2->TDR = c;
}

void UART_SendString(const char *str)
{
    while (*str)
    {
        UART_SendChar(*str++);
    }
}

char UART_GetChar(void)
{
    while (!(USART2->ISR & USART_ISR_RXNE_RXFNE))
    {
    }

    return (char)USART2->RDR;
}