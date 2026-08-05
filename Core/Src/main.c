#include "stm32f429xx.h"

#include "uart.h"
#include "f429_sdram.h"
#include "ltdc_ili9341.h"

static void Delay(volatile uint32_t count)
{
    while (count--)
    {
        __asm__("nop");
    }
}

int main(void)
{
    /* STM32F429I-DISC1 user LEDs */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOGEN;

    GPIOG->MODER &= ~(
        (3U << (13 * 2)) |
        (3U << (14 * 2)));

    GPIOG->MODER |=
        (1U << (13 * 2)) |
        (1U << (14 * 2));

    UART_Init();

    UART_SendString("\r\n");
    UART_SendString("================================\r\n");
    UART_SendString(" STM32F429 LCD BRINGUP\r\n");
    UART_SendString("================================\r\n");

    while (1)
    {
        GPIOG->ODR ^= (1U << 13);
        GPIOG->ODR ^= (1U << 14);

        UART_SendString("Alive\r\n");

        Delay(1000000);
    }
}