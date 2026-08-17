#include "stm32f4xx_hal.h"
#include "uart.h"

#define UART_BAUD_RATE 115200U

void UART_Init(void)
{
    uint32_t pclk2;

    /* Enable GPIOA and USART1 peripheral clocks. */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    /* PA9 = USART1_TX and PA10 = USART1_RX, alternate function AF7. */
    GPIOA->MODER &= ~((3U << (9U * 2U)) |
                      (3U << (10U * 2U)));
    GPIOA->MODER |=  ((2U << (9U * 2U)) |
                      (2U << (10U * 2U)));

    GPIOA->OTYPER &= ~((1U << 9U) |
                       (1U << 10U));

    GPIOA->OSPEEDR &= ~((3U << (9U * 2U)) |
                        (3U << (10U * 2U)));
    GPIOA->OSPEEDR |=  ((3U << (9U * 2U)) |
                        (3U << (10U * 2U)));

    GPIOA->PUPDR &= ~((3U << (9U * 2U)) |
                      (3U << (10U * 2U)));
    GPIOA->PUPDR |=  (1U << (10U * 2U));

    GPIOA->AFR[1] &= ~((0xFU << 4U) |
                       (0xFU << 8U));
    GPIOA->AFR[1] |=  ((7U << 4U) |
                       (7U << 8U));

    /* Disable USART1 while changing its configuration. */
    USART1->CR1 = 0U;
    USART1->CR2 = 0U;
    USART1->CR3 = 0U;

    /*
     * USART1 is clocked from APB2. With oversampling by 16, the STM32F4 BRR
     * register is the rounded APB2 clock divided by the requested baud rate.
     * This works both at the reset 16 MHz clock and after SystemClock_Config().
     */
    pclk2 = HAL_RCC_GetPCLK2Freq();
    USART1->BRR = (pclk2 + (UART_BAUD_RATE / 2U)) / UART_BAUD_RATE;

    /* 8 data bits, no parity, one stop bit, TX and RX enabled. */
    USART1->CR1 = USART_CR1_TE |
                  USART_CR1_RE |
                  USART_CR1_UE;

    /* Clear a possible stale status/data condition. */
    (void)USART1->SR;
    (void)USART1->DR;
}

void UART_SendChar(char c)
{
    while ((USART1->SR & USART_SR_TXE) == 0U)
    {
    }

    USART1->DR = (uint8_t)c;
}

void UART_SendString(const char *s)
{
    if (s == NULL)
    {
        return;
    }

    while (*s != '\0')
    {
        UART_SendChar(*s);
        s++;
    }

    UART_Flush();
}

void UART_Flush(void)
{
    while ((USART1->SR & USART_SR_TC) == 0U)
    {
    }
}

void UART_SendHex32(uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    for (int32_t shift = 28; shift >= 0; shift -= 4)
    {
        UART_SendChar(hex[(value >> (uint32_t)shift) & 0x0FU]);
    }

    UART_Flush();
}

void UART_SendDec(uint32_t value)
{
    char buffer[11];
    uint32_t index = sizeof(buffer);

    buffer[--index] = '\0';

    do
    {
        buffer[--index] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (value != 0U);

    UART_SendString(&buffer[index]);
}
int UART_CharAvailable(void)
{
    return ((USART1->SR & USART_SR_RXNE) != 0U);
}

char UART_ReadChar(void)
{
    while ((USART1->SR & USART_SR_RXNE) == 0U)
    {
    }

    return (char)(USART1->DR & 0xFFU);
}