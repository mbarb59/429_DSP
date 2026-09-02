#!/usr/bin/env python3
"""Safely clean and normalize the SD-card diagnostics in 429_DSP.

Usage from the project root:
  python3 sd_project_manager.py audit
  python3 sd_project_manager.py apply
  python3 sd_project_manager.py build
  python3 sd_project_manager.py restore <backup-directory>

The apply command:
  * creates a timestamped backup;
  * removes obsolete SD experiments;
  * preserves sdspi, sdcmd0, sdbitcmd0, and sdbitloop;
  * installs the known-good Mode-0 bit-bang byte routine;
  * installs the known-good CMD0 transaction that produced R1=0x01;
  * removes chat/HTML artifacts and trailing whitespace;
  * validates function counts and brace balance;
  * writes files atomically;
  * runs git diff --check;
  * restores the backup automatically if validation fails.
"""

from __future__ import annotations

import argparse
import datetime as dt
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path.cwd()
MAIN = ROOT / "Core/Src/main.c"
SD_C = ROOT / "Core/Src/sd_spi.c"
SD_H = ROOT / "Core/Inc/sd_spi.h"
FILES = (MAIN, SD_C, SD_H)

OBSOLETE_FUNCTIONS = (
    "Console_RunSdSpiLoopback",
    "Console_RunSdStaticLoopback",
    "Console_RunSdHalLoopback",
    "Console_RunSdSpiBurst",
)
OBSOLETE_COMMANDS = ("sdloop", "sdstatic", "sdhalloop", "sdburst")
REQUIRED_COMMANDS = ("sdspi", "sdcmd0", "sdbitcmd0", "sdbitloop")

SOFTWARE_DELAY = r'''static void SD_SoftwareDelay(void)
{
    for (volatile uint32_t index = 0U;
         index < 1200U;
         index++)
    {
        __NOP();
    }
}'''

SOFTWARE_TRANSFER = r'''static uint8_t SD_SoftwareTransferByte(uint8_t transmit_byte)
{
    uint8_t receive_byte = 0U;

    for (uint32_t bit = 0U; bit < 8U; bit++)
    {
        /* SPI Mode 0: prepare MOSI while SCK is low. */
        GPIOB->BSRR = (uint32_t)GPIO_PIN_13 << 16U;

        if ((transmit_byte & 0x80U) != 0U)
        {
            GPIOB->BSRR = GPIO_PIN_15;
        }
        else
        {
            GPIOB->BSRR = (uint32_t)GPIO_PIN_15 << 16U;
        }

        transmit_byte <<= 1U;
        SD_SoftwareDelay();

        /* Rising edge and a short propagation allowance. */
        GPIOB->BSRR = GPIO_PIN_13;
        __NOP();
        __NOP();
        __NOP();
        __NOP();
        __NOP();
        __NOP();
        __NOP();
        __NOP();

        receive_byte <<= 1U;
        if ((GPIOB->IDR & GPIO_PIN_14) != 0U)
        {
            receive_byte |= 0x01U;
        }

        SD_SoftwareDelay();
        GPIOB->BSRR = (uint32_t)GPIO_PIN_13 << 16U;
        SD_SoftwareDelay();
    }

    return receive_byte;
}'''

CMD0_FUNCTION = r'''static void Console_RunSdSoftwareCmd0(void)
{
    uint8_t response_bytes[16] = {0U};
    GPIO_InitTypeDef gpio = {0};
    uint8_t response = 0xFFU;
    bool response_received = false;
    uint32_t primask;

    UART_SendString("Software SPI CMD0 Test\r\n");
    UART_SendString("----------------------\r\n");

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    SPI2->CR1 &= ~SPI_CR1_SPE;

    /* PA5 = CS, initially high. */
    GPIOA->BSRR = GPIO_PIN_5;
    gpio.Pin = GPIO_PIN_5;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* PB13 = SCK low, PB15 = MOSI high. */
    GPIOB->BSRR = (uint32_t)GPIO_PIN_13 << 16U;
    GPIOB->BSRR = GPIO_PIN_15;
    gpio.Pin = GPIO_PIN_13 | GPIO_PIN_15;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* PB14 = MISO with idle pull-up. */
    gpio.Pin = GPIO_PIN_14;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);

    HAL_Delay(100U);

    primask = __get_PRIMASK();
    __disable_irq();

    /* CS high and MOSI high: provide 80 startup clocks. */
    GPIOA->BSRR = GPIO_PIN_5;
    GPIOB->BSRR = GPIO_PIN_15;
    for (uint32_t index = 0U; index < 10U; index++)
    {
        (void)SD_SoftwareTransferByte(0xFFU);
    }

    /* Select the card. Do not send an idle byte after CS goes low. */
    GPIOA->BSRR = (uint32_t)GPIO_PIN_5 << 16U;
    SD_SoftwareDelay();

    /* CMD0 = 40 00 00 00 00 95. */
    (void)SD_SoftwareTransferByte(0x40U);
    (void)SD_SoftwareTransferByte(0x00U);
    (void)SD_SoftwareTransferByte(0x00U);
    (void)SD_SoftwareTransferByte(0x00U);
    (void)SD_SoftwareTransferByte(0x00U);
    (void)SD_SoftwareTransferByte(0x95U);

    for (uint32_t index = 0U; index < 16U; index++)
    {
        response_bytes[index] = SD_SoftwareTransferByte(0xFFU);
    }

    GPIOA->BSRR = GPIO_PIN_5;
    (void)SD_SoftwareTransferByte(0xFFU);

    if (primask == 0U)
    {
        __enable_irq();
    }

    for (uint32_t index = 0U; index < 16U; index++)
    {
        if ((response_bytes[index] & 0x80U) == 0U)
        {
            response = response_bytes[index];
            response_received = true;
            break;
        }
    }

    UART_SendString("Raw Response  = ");
    for (uint32_t index = 0U; index < 16U; index++)
    {
        UART_SendHex32((uint32_t)response_bytes[index]);
        UART_SendChar(' ');
    }
    UART_SendString("\r\n");

    UART_SendString("R1 Response   = ");
    UART_SendHex32((uint32_t)response);
    UART_SendString("\r\n");
    UART_SendString("Card State    = ");

    if (response_received && (response == 0x01U))
    {
        UART_SendString("IDLE\r\n");
    }
    else if (response_received)
    {
        UART_SendString("UNEXPECTED RESPONSE\r\n");
    }
    else
    {
        UART_SendString("NO RESPONSE\r\n");
    }

    UART_SendString("Reset board after software SPI test\r\n");
}'''


def abort(message: str) -> None:
    raise RuntimeError(message)


def run(command: list[str], check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, text=True, capture_output=True, check=check)


def clean_text(text: str) -> str:
    replacements = {
        "&lt;": "<",
        "&gt;": ">",
        "&amp;": "&",
        "<br>": "\n",
        "<br/>": "\n",
        "<br />": "\n",
    }
    for old, new in replacements.items():
        text = text.replace(old, new)
    return "\n".join(line.rstrip() for line in text.splitlines()).rstrip() + "\n"


def block_end(text: str, open_brace: int) -> int:
    depth = 0
    state = "code"
    escaped = False
    index = open_brace
    while index < len(text):
        char = text[index]
        nxt = text[index + 1] if index + 1 < len(text) else ""
        if state == "line":
            if char == "\n":
                state = "code"
        elif state == "block":
            if char == "*" and nxt == "/":
                state = "code"
                index += 1
        elif state == "string":
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                state = "code"
        elif state == "char":
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == "'":
                state = "code"
        else:
            if char == "/" and nxt == "/":
                state = "line"
                index += 1
            elif char == "/" and nxt == "*":
                state = "block"
                index += 1
            elif char == '"':
                state = "string"
            elif char == "'":
                state = "char"
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return index + 1
        index += 1
    abort("Unclosed C brace block")


def function_span(text: str, name: str) -> tuple[int, int]:
    pattern = re.compile(
        rf"(?m)^static\s+[\w\s*]+\b{re.escape(name)}\s*\([^;]*\)\s*\{{"
    )
    matches = list(pattern.finditer(text))
    if len(matches) != 1:
        abort(f"Expected one definition of {name}, found {len(matches)}")
    match = matches[0]
    brace = text.find("{", match.start(), match.end())
    return match.start(), block_end(text, brace)


def replace_function(text: str, name: str, replacement: str) -> str:
    start, end = function_span(text, name)
    return text[:start] + replacement.rstrip() + "\n\n" + text[end:].lstrip("\n")


def remove_function(text: str, name: str) -> str:
    start, end = function_span(text, name)
    return text[:start] + text[end:].lstrip("\n")


def command_span(text: str, command: str) -> tuple[int, int]:
    pattern = re.compile(
        rf'(?m)^\s*else if \(strcmp\(command, "{re.escape(command)}"\) == 0\)\s*\{{'
    )
    matches = list(pattern.finditer(text))
    if len(matches) != 1:
        abort(f"Expected one handler for {command}, found {len(matches)}")
    match = matches[0]
    brace = text.find("{", match.start(), match.end())
    return match.start(), block_end(text, brace)


def remove_command(text: str, command: str) -> str:
    start, end = command_span(text, command)
    return text[:start] + text[end:].lstrip("\n")


def backup_files() -> Path:
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    destination = ROOT / f"sd_manager_backup_{stamp}"
    destination.mkdir()
    for source in FILES:
        shutil.copy2(source, destination / source.name)
    return destination


def restore_backup(directory: Path) -> None:
    mapping = {"main.c": MAIN, "sd_spi.c": SD_C, "sd_spi.h": SD_H}
    for filename, destination in mapping.items():
        source = directory / filename
        if not source.exists():
            abort(f"Backup file missing: {source}")
        shutil.copy2(source, destination)


def atomic_write(path: Path, text: str) -> None:
    with tempfile.NamedTemporaryFile("w", dir=path.parent, delete=False) as handle:
        handle.write(text)
        temporary = Path(handle.name)
    temporary.replace(path)


def audit() -> bool:
    okay = True
    for path in FILES:
        if not path.exists():
            print(f"MISSING: {path}")
            okay = False
    if not okay:
        return False

    main = MAIN.read_text(encoding="utf-8")
    artifacts = re.findall(r"&lt;|&gt;|&amp;|<br\s*/?>", main)
    print(f"HTML artifacts in main.c: {len(artifacts)}")

    for command in REQUIRED_COMMANDS:
        count = main.count(f'strcmp(command, "{command}")')
        print(f"Command {command}: {count}")
        okay &= count == 1

    for command in OBSOLETE_COMMANDS:
        count = main.count(f'strcmp(command, "{command}")')
        print(f"Obsolete command {command}: {count}")

    for name in ("SD_SoftwareDelay", "SD_SoftwareTransferByte", "Console_RunSdSoftwareCmd0"):
        try:
            function_span(main, name)
            print(f"Function {name}: OK")
        except RuntimeError as error:
            print(f"Function {name}: {error}")
            okay = False

    diff = run(["git", "--no-pager", "diff", "--check"], check=False)
    print("git diff --check:", "OK" if diff.returncode == 0 else "FAILED")
    if diff.stdout:
        print(diff.stdout, end="")
    if diff.stderr:
        print(diff.stderr, end="")
    okay &= diff.returncode == 0
    return okay


def apply() -> None:
    for path in FILES:
        if not path.exists():
            abort(f"Required file missing: {path}")

    backup = backup_files()
    print(f"Backup created: {backup}")

    try:
        main = clean_text(MAIN.read_text(encoding="utf-8"))
        sd_c = clean_text(SD_C.read_text(encoding="utf-8"))
        sd_h = clean_text(SD_H.read_text(encoding="utf-8"))

        for name in OBSOLETE_FUNCTIONS:
            try:
                main = remove_function(main, name)
            except RuntimeError:
                pass
            main = re.sub(
                rf"(?m)^static\s+[\w\s*]+\b{re.escape(name)}\s*\([^;]*\);\s*\n",
                "",
                main,
                count=1,
            )

        for command in OBSOLETE_COMMANDS:
            try:
                main = remove_command(main, command)
            except RuntimeError:
                pass
            main = re.sub(
                rf'(?m)^\s*"{re.escape(command)}\s+.*?\\r\\n"\s*\n',
                "",
                main,
                count=1,
            )

        main = replace_function(main, "SD_SoftwareDelay", SOFTWARE_DELAY)
        main = replace_function(main, "SD_SoftwareTransferByte", SOFTWARE_TRANSFER)
        main = replace_function(main, "Console_RunSdSoftwareCmd0", CMD0_FUNCTION)
        main = clean_text(main)

        if any(token in main for token in ("&lt;", "&gt;", "&amp;", "<br")):
            abort("Markup artifacts remain after cleanup")

        atomic_write(MAIN, main)
        atomic_write(SD_C, sd_c)
        atomic_write(SD_H, sd_h)

        diff = run(["git", "--no-pager", "diff", "--check"], check=False)
        if diff.returncode != 0:
            abort("git diff --check failed:\n" + diff.stdout + diff.stderr)

        for command in REQUIRED_COMMANDS:
            if main.count(f'strcmp(command, "{command}")') != 1:
                abort(f"Required command count invalid: {command}")

        print("Apply completed successfully.")
        print("Next: python3 sd_project_manager.py build")
    except Exception:
        restore_backup(backup)
        print(f"Apply failed. Restored backup: {backup}", file=sys.stderr)
        raise


def build() -> None:
    print("Running make clean...")
    subprocess.run(["make", "clean"], check=True)
    print("Building...")
    process = subprocess.run(["make"], text=True)
    if process.returncode != 0:
        abort("Build failed")
    binary = ROOT / "build/429_DSP.bin"
    if not binary.exists():
        abort(f"Build completed but binary is missing: {binary}")
    print(f"Build successful: {binary} ({binary.stat().st_size} bytes)")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("audit")
    sub.add_parser("apply")
    sub.add_parser("build")
    restore = sub.add_parser("restore")
    restore.add_argument("directory", type=Path)
    args = parser.parse_args()

    try:
        if args.command == "audit":
            raise SystemExit(0 if audit() else 1)
        if args.command == "apply":
            apply()
        elif args.command == "build":
            build()
        elif args.command == "restore":
            restore_backup(args.directory)
            print(f"Restored: {args.directory}")
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
