#include "stm32f429xx.h"
#include "uart.h"

void UART_Init(void)
{
    /* Enable GPIOA */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    /* Enable USART1 */
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    /* PA9 = TX */
    GPIOA->MODER &= ~(3U << (9 * 2));
    GPIOA->MODER |=  (2U << (9 * 2));

    /* PA10 = RX */
    GPIOA->MODER &= ~(3U << (10 * 2));
    GPIOA->MODER |=  (2U << (10 * 2));

    /* AF7 = USART1 */
    GPIOA->AFR[1] &= ~((0xF << 4) | (0xF << 8));
    GPIOA->AFR[1] |=  ((7 << 4) | (7 << 8));

    /* 115200 baud @ 16 MHz HSI */
    USART1->BRR = 139;

    USART1->CR1 =
        USART_CR1_TE |
        USART_CR1_RE |
        USART_CR1_UE;
}

void UART_SendChar(char c)
{
    while (!(USART1->SR & USART_SR_TXE))
    {
    }

    USART1->DR = c;
}

void UART_SendString(const char *str)
{
    while (*str)
    {
        UART_SendChar(*str++);
    }
}