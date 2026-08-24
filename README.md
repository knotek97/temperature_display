# MCSD Project — Scrolling Temperature Display

Microcontroller project, Bachelor Electronics, FH Technikum Wien.

| | |
|---|---|
| **Alexander Knotek** | Thermo 3 Click — input module, I²C |
| **Anton Wittig** | 8x8 R Click — output module, SPI |

The system measures the ambient temperature and scrolls the complete reading,
including the unit, through an 8x8 LED matrix. Because only one character fits
on the display at a time, the value is rendered as a text string such as
`23.4°C` and moved pixel by pixel from right to left. The button on the shield
switches between Celsius and Fahrenheit.

Both Click Boards can also be tested independently from a PC over a UART text
menu, without the other board being present.

---

## Status

| Part | State |
|---|---|
| TMP102 driver (`thermo3.c/.h`) | working |
| 1 s sampling over TIM6 | working |
| ALERT over EXTI | working, see note below |
| UART test menu for the sensor | working |
| MAX7219 driver (`led8x8.c/.h`) | in progress (Anton) |
| Scrolling text and font | in progress (Anton) |
| Button for °C / °F | not started |
| Standby after 30 s | not started |

---

## Hardware

- **NUCLEO-L432KC** (STM32L432KC, Cortex-M4, 80 MHz)
- **Nucleo32 Click Shield**
- **Thermo 3 Click** (MIKROE-1885, TMP102) in **click port 2**
- **8x8 R Click** (MIKROE-1295, MAX7219) in **click port 1**

### Why the boards cannot be swapped

The shield ties the I²C lines to two pins of click port 2: `A5` is connected to
`D5` (SCL) and `A4` to `D4` (SDA). A SPI click on port 2 would drive its chip
select on those lines and kill the I²C bus. The shield documentation states the
recommended setup directly: SPI on port 1, I²C on port 2.

### Pin assignment

| Function | Arduino | MCU | Peripheral |
|---|---|---|---|
| I²C SCL (Thermo 3) | D5 | PB6 | I2C1_SCL |
| I²C SDA (Thermo 3) | D4 | PB7 | I2C1_SDA |
| ALERT (Thermo 3) | A3 | PA4 | GPIO_EXTI4 |
| SPI SCK (8x8) | D13 | PB3 | SPI1_SCK |
| SPI MOSI (8x8) | D11 | PB5 | SPI1_MOSI |
| CS / LOAD (8x8) | D3 | PB0 | GPIO_Output |
| Button SW1 | A2 | PA3 | GPIO_EXTI3 |
| VCP TX | — | PA2 | USART2_TX |
| VCP RX | — | PA15 | USART2_RX |

`PA15` is not routed to the headers; it is wired to the ST-Link virtual COM
port on the board itself.

### Two hardware notes worth knowing

`PA4` is also the blue channel of the RGB LED on the shield. The shield
documentation says this pin cannot be pulled below roughly 600–800 mV. The low
threshold of the STM32 is about 1.0 V, so the ALERT signal is still recognised
— but it works with less margin than a clean pin would. If the interrupt ever
misbehaves, `thermo3_alert_active()` can read the alert flag over I²C instead
and the EXTI can be dropped entirely.

`PB3` carries both SPI1_SCK and the green user LED LD3. The LED will flicker
whenever the display is written to. Harmless, but do not use `BSP_LED_*` on it
once the display module is in.

---

## Toolchain and CubeMX

STM32CubeMX for the pin configuration, STM32CubeIDE (HAL) for the firmware.
Created through the Board Selector with "initialize all peripherals with their
default mode".

| Setting | Value |
|---|---|
| SYS → Debug | Serial Wire |
| RCC → HSE | **Disable** — the Nucleo-32 has no crystal |
| SYSCLK | 80 MHz (MSI 4 MHz → PLL, `PLLM 1 / PLLN 40 / PLLR 2`) |
| I2C1 | Standard Mode, 100 kHz, analog filter on |
| USART2 | 115200, 8N1, TX + RX |
| TIM6 | Prescaler `7999`, Period `9999` → exactly 1 Hz |
| NVIC | TIM6 global interrupt, EXTI line4 interrupt |

The TIM6 values are "wanted minus one" because the counter includes zero:
80 MHz / 8000 = 10 kHz, and 10 kHz / 10000 = 1 Hz.

Under Project Manager → Code Generator, **Keep User Code when re-generating**
must be ticked, otherwise every change to the `.ioc` file deletes the
application code in `main.c`.

---

## Project structure

```
Core/Inc/thermo3.h      TMP102 driver, public interface
Core/Src/thermo3.c      TMP102 driver, implementation
Core/Src/main.c         application layer: sampling, UART menu, callbacks
```

The rule we follow: everything that knows a hardware detail of a Click Board
lives in that board's driver module, and nothing else does. `thermo3.c` is the
only file that knows the I²C address, the register numbers and how the 12 bits
are packed. It returns Celsius and knows nothing about the display, about
Fahrenheit or about the UART. That separation is what makes it possible to test
each board on its own.

### Driver interface

```c
uint8_t thermo3_init(I2C_HandleTypeDef *hi2c);
uint8_t thermo3_read_raw(int16_t *raw);
int32_t thermo3_raw_to_centi_c(int16_t raw);
int32_t thermo3_centi_c_to_centi_f(int32_t centi_c);
uint8_t thermo3_read_celsius(float *celsius);
uint8_t thermo3_set_limits(int32_t low_centi_c, int32_t high_centi_c);
void    thermo3_set_alert_pin(GPIO_TypeDef *port, uint16_t pin);
uint8_t thermo3_alert_active(void);
```

All functions return `THERMO3_OK` (0) on success and `THERMO3_ERROR` (1) on
failure. Temperatures are passed as `int32_t` in **hundredths of a degree
Celsius** — `2344` means 23.44 °C.

---

## Building and running

1. Open the project in STM32CubeIDE.
2. `Project → Build All`.
3. `Run → Debug` or `Run As → STM32 C/C++ Application`.
4. Open a terminal on the ST-Link virtual COM port at **115200 8N1**.

The terminal does not echo what you type — that is normal, local echo is off by
default. The menu still reacts.

---

## UART test interface

```
=== MCSD Thermo 3 Click - test menu ===
 [1] read temperature once
 [2] start / stop continuous output (1 s)
 [3] switch unit C / F
 [4] set alert limits (type them in)
 [8] show status
 [m] show this menu again
```

Example session:

```
MCSD project - Thermo 3 Click
scanning i2c bus...
  found device at 0x48
OK;sensor ready

> TEMP;4231;23.44;C
> OK;unit=F
> TEMP;9102;74.19;F

TLOW in degC (e.g. 28 or 28.5), Enter to confirm: 25
THIGH in degC: 27
OK;tlow=25.00;thigh=27.00

ALERT;1
ALERT;0
```

### Output format

Measurements are semicolon separated so a terminal log can be saved and opened
in Excel:

```
TEMP;<tick_ms>;<value>;<unit>       TEMP;12345;23.44;C
STATUS;unit=C;streaming=0;tlow=28.00;thigh=30.00;alert=0
ALERT;<0 or 1>
OK;<what happened>
ERR;<what went wrong>
```

### Testing each board independently

**Sensor without display:** commands `1` and `2` print measurements over UART,
so the input module can be verified with no display connected at all.

**Display without sensor:** the display commands scroll any text typed by the
tester, so the output module can be verified with no valid measurement.

---

## Design decisions

**Integers instead of floats.** Temperatures are handled as hundredths of a
degree everywhere. `printf` on the STM32 needs an extra linker flag to print a
float and costs a lot of flash, so the number formatting is written by hand in
`uart_put_centi()`. `thermo3_read_celsius()` exists for completeness but is not
used in the data path.

**Multiplication before division.** `thermo3_raw_to_centi_c()` computes
`(raw * 625) / 100`, not `raw * 6.25`. Dividing first would truncate every
result to zero. The multiplication is done in 32 bit because it overflows in
16 bit.

**Sign extension.** The temperature register is 12 bit, left justified. After
shifting it right, bit 11 is the sign bit, and for negative values the four bits
above it must be set manually — otherwise a small negative temperature reads as
a large positive one.

**Comparator mode with two limits.** `TLOW` and `THIGH` are deliberately
different (default 28 °C and 30 °C). The gap is the hysteresis: without it the
alert would flicker on and off when the temperature sits exactly on the
threshold.

**Work in the main loop, not in the interrupt.** The TIM6 and EXTI callbacks
only set a flag. Sending a line over UART with a 100 ms timeout inside an
interrupt handler would block everything else.

**`volatile` on the interrupt flags.** The interrupt writes them and the main
loop reads them. Without `volatile` the optimiser may cache the value in a
register and the main loop would never see the change. At `-O0` this is
invisible; at `-O2` it breaks.

**No `HAL_Delay` in the loop.** It would block, and the UART menu would only
react once per second.

---

## Troubleshooting

| Symptom | Cause |
|---|---|
| `ERR;sensor not found` | wrong ADD jumper position, or the click sits in port 1 |
| I²C scanner finds nothing | SDA and SCL swapped, or the shield is not seated |
| Scanner finds `0x49` | ADD jumper in the other position — change `TMP102_I2C_ADDR` |
| Menu stops reacting after fast typing | UART overrun, `__HAL_UART_CLEAR_OREFLAG` missing |
| Board completely silent | `thermo3_init()` not called in `USER CODE BEGIN 2` |
| `'huart2' undeclared` | code in a separate file without the `extern` declarations |
| Everything gone after CubeMX regenerate | code outside a `USER CODE` block |

---

## Sources used

- TI TMP102 datasheet (SBOS397) — register layout, data format, ALERT behaviour
- Maxim MAX7219 datasheet — 16-bit frame format, register map
- MikroE Thermo 3 Click and 8x8 R Click product pages
- Nucleo32 Click Shield pin mapping (2025-03-26), lecture material
- UM1956, NUCLEO-32 user manual — virtual COM port pins

External libraries were not used. The drivers were written against the
datasheets so that every line can be explained.
