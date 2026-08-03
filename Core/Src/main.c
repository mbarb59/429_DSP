#include "stm32g431xx.h"
#include "uart.h"
#include "console.h"
#include "adc.h"
#include "capture.h"
#include "fft.h"

volatile int led_enabled = 1;

static void delay(void)
{
    for (volatile uint32_t i = 0; i < 200000; i++)
    {
        __asm__("nop");
    }
}

int main(void)
{
    char cmd_buffer[64];
    uint32_t cmd_index = 0;

    /* PB8 LED */
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;

    GPIOB->MODER &= ~(3UL << (8 * 2));
    GPIOB->MODER |=  (1UL << (8 * 2));

    UART_Init();
    ADC_Init();
    DMA_Init();
 

    NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    
UART_SendString(
        "\r\n"
        "431 DSP Analyzer\r\n"
        "Version 0.5\r\n"
        "\r\n"
        "Type 'help' for commands\r\n"
        "\r\n"
        "> ");

    while (1)
    {
        if (USART2->ISR & USART_ISR_RXNE_RXFNE)
        {
            char c = UART_GetChar();

            if ((c == '\r') || (c == '\n'))
            {
                cmd_buffer[cmd_index] = '\0';

                UART_SendString("\r\n");

                Console_ProcessCommand(cmd_buffer);

                cmd_index = 0;
            }
            else
            {
                UART_SendChar(c);

                if (cmd_index < (sizeof(cmd_buffer) - 1))
                {
                    cmd_buffer[cmd_index++] = c;
                }
            }
        }

        if (led_enabled)
        {
            GPIOB->ODR ^= (1UL << 8);
        }

        delay();
    }
}