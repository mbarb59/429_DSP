#include "stm32f429xx.h"

int main(void)
{
    /* Enable GPIOG clock */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOGEN;

    /* PG13 and PG14 outputs */
    GPIOG->MODER &= ~((3U << (13 * 2)) | (3U << (14 * 2)));

    GPIOG->MODER |=
        (1U << (13 * 2)) |
        (1U << (14 * 2));

    while (1)
    {
        GPIOG->ODR ^= (1U << 13);
        GPIOG->ODR ^= (1U << 14);

        for (volatile uint32_t i = 0; i < 1000000; i++)
        {
        }
    }
}