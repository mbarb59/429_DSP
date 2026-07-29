#include <stdint.h>
#include <string.h>

#include "console.h"
#include "uart.h"
#include "adc.h"

extern volatile int led_enabled;

static void UART_SendUInt(uint32_t value)
{
    char buf[16];
    int i = 0;

    if (value == 0)
    {
        UART_SendChar('0');
        return;
    }

    while (value > 0)
    {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }

    while (i > 0)
    {
        UART_SendChar(buf[--i]);
    }
}

void Console_ProcessCommand(char *cmd)
{
    if (strcmp(cmd, "help") == 0)
    {
        UART_SendString(
            "\r\n"
            "431 DSP Analyzer Commands\r\n"
            "-------------------------\r\n"
            "help       - Show command list\r\n"
            "version    - Show firmware version\r\n"
            "status     - Show system status\r\n"
            "adc        - Read ADC channel\r\n"
            "adcstream  - Stream 32 ADC samples\r\n"
            "led on     - Enable heartbeat LED\r\n"
            "led off    - Disable heartbeat LED\r\n");
    }
    else if (strcmp(cmd, "version") == 0)
    {
        UART_SendString(
            "431 DSP Analyzer\r\n"
            "Version 0.6\r\n");
    }
    else if (strcmp(cmd, "status") == 0)
    {
        UART_SendString(
            "Board: STM32G431KB\r\n"
            "Status: OK\r\n");
    }
    else if (strcmp(cmd, "adc") == 0)
    {
        uint16_t value = ADC_Read();

        UART_SendString("ADC1 = ");
        UART_SendUInt(value);
        UART_SendString("\r\n");
    }
    else if (strcmp(cmd, "adcstream") == 0)
    {
        UART_SendString("Streaming 32 samples:\r\n");

        for (int i = 0; i < 32; i++)
        {
            uint16_t value = ADC_Read();

            UART_SendUInt(value);
            UART_SendString("\r\n");

            for (volatile uint32_t d = 0; d < 50000; d++)
            {
                __asm__("nop");
            }
        }
    }
    else if (strcmp(cmd, "led on") == 0)
    {
        led_enabled = 1;
        UART_SendString("LED Enabled\r\n");
    }
    else if (strcmp(cmd, "led off") == 0)
    {
        led_enabled = 0;
        UART_SendString("LED Disabled\r\n");
    }
    else if (cmd[0] != '\0')
    {
        UART_SendString("Unknown Command\r\n");
    }

    UART_SendString("> ");
}