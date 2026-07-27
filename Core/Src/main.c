#include "stm32g431xx.h"
#include "uart.h"
#include "adc.h"

static void delay(void)
{
    for (volatile uint32_t i = 0; i < 200000; i++)
    {
        __asm__("nop");
    }
}

int main(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;

    /* PB8 LED */
    GPIOB->MODER &= ~(3UL << (8 * 2));
    GPIOB->MODER |=  (1UL << (8 * 2));

    UART_Init();

    UART_SendString("UART OK\r\n");

    ADC_Init();

    UART_SendString("ADC INIT DONE\r\n");

    while (1)
    {
        GPIOB->ODR ^= (1UL << 8);

        UART_SendString("X");

        delay();
    }
}