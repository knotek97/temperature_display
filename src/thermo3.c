/*
 * thermo3.c
 *
 * Driver for the Thermo 3 Click (TMP102).
 * Everything in here comes from the TMP102 datasheet from TI.
 */

#include "thermo3.h"

/* ------------------------------------------------------------------ */
/* Constants from the datasheet                                       */
/* ------------------------------------------------------------------ */

/* The TMP102 has the 7 bit address 0x48 when the ADD jumper is in the
 * default position. The HAL wants the address already shifted one to the
 * left (the lowest bit is the read/write bit), so 0x48 << 1 = 0x90.
 * This cost me some time, in the datasheet it says 0x48. */
#define TMP102_I2C_ADDR   (0x48u << 1)

/* Register numbers (this is what the datasheet calls "pointer register") */
#define TMP102_REG_TEMP   0x00u
#define TMP102_REG_CONF   0x01u
#define TMP102_REG_TLOW   0x02u
#define TMP102_REG_THIGH  0x03u

/* Configuration we write at the start. The TMP102 has a 16 bit config
 * register, sent as two bytes, high byte first:
 *
 *   high byte 0x60 = 0110 0000
 *        bit 7 OS   = 0  no one-shot, we want it running all the time
 *        bit 6 R1   = 1  \ converter resolution, these two are fixed to 11
 *        bit 5 R0   = 1  /
 *        bit 4 F1   = 0  \ fault queue: 1 fault is enough to trigger ALERT
 *        bit 3 F0   = 0  /
 *        bit 2 POL  = 0  ALERT pin is active LOW
 *        bit 1 TM   = 0  comparator mode -> this gives us the hysteresis
 *        bit 0 SD   = 0  no shutdown, continuous conversion
 *
 *   low byte 0xA0 = 1010 0000
 *        bit 7 CR1  = 1  \ conversion rate 4 Hz (the default)
 *        bit 6 CR0  = 0  /
 *        bit 5 AL   = 1  read only, this is the alert flag
 *        bit 4 EM   = 0  normal 12 bit mode, no extended mode
 */
#define TMP102_CONF_HIGH  0x60u
#define TMP102_CONF_LOW   0xA0u

/* How long we wait for the I2C transfer before we give up (milliseconds).
 * 100 ms is very generous, a transfer of 2 bytes takes far below 1 ms. */
#define TMP102_I2C_TIMEOUT 100u

/* ------------------------------------------------------------------ */
/* Module variables                                                    */
/* ------------------------------------------------------------------ */

/* static means: only this file can see it. The pointer to the I2C handle
 * is stored here in thermo3_init(), so the other functions do not need it
 * as a parameter every time. */
static I2C_HandleTypeDef *thermo3_i2c = 0;

/* Where the ALERT line is wired. Stays 0 if I never register a pin, then
 * thermo3_alert_active() asks the sensor over I2C instead. */
static GPIO_TypeDef *thermo3_alert_port = 0;
static uint16_t      thermo3_alert_gpio = 0;

/* ------------------------------------------------------------------ */
/* Small helpers, only used inside this file                           */
/* ------------------------------------------------------------------ */

/* Writes 2 bytes into one register of the sensor. */
static uint8_t thermo3_write_reg(uint8_t reg, uint8_t high, uint8_t low)
{
    uint8_t data[2];

    data[0] = high;   /* the TMP102 always wants the high byte first */
    data[1] = low;

    if (HAL_I2C_Mem_Write(thermo3_i2c, TMP102_I2C_ADDR, reg,
                          I2C_MEMADD_SIZE_8BIT, data, 2,
                          TMP102_I2C_TIMEOUT) != HAL_OK)
    {
        return THERMO3_ERROR;
    }

    return THERMO3_OK;
}

/* Reads 2 bytes out of one register.
 * HAL_I2C_Mem_Read does the whole thing in one call: it writes the register
 * number, then makes a repeated start and reads the two bytes. Doing it by
 * hand with Master_Transmit + Master_Receive also works, but then I have to
 * take care of the repeated start myself, so I use the Mem version. */
static uint8_t thermo3_read_reg(uint8_t reg, uint8_t *high, uint8_t *low)
{
    uint8_t data[2];

    if (HAL_I2C_Mem_Read(thermo3_i2c, TMP102_I2C_ADDR, reg,
                         I2C_MEMADD_SIZE_8BIT, data, 2,
                         TMP102_I2C_TIMEOUT) != HAL_OK)
    {
        return THERMO3_ERROR;
    }

    *high = data[0];
    *low  = data[1];

    return THERMO3_OK;
}

/* ------------------------------------------------------------------ */
/* Public functions                                                    */
/* ------------------------------------------------------------------ */

uint8_t thermo3_init(I2C_HandleTypeDef *hi2c)
{
    uint8_t high = 0;
    uint8_t low  = 0;

    /* Without a handle nothing works, so I check it. */
    if (hi2c == 0)
    {
        return THERMO3_ERROR;
    }

    thermo3_i2c = hi2c;

    /* Write our configuration. */
    if (thermo3_write_reg(TMP102_REG_CONF,
                          TMP102_CONF_HIGH, TMP102_CONF_LOW) != THERMO3_OK)
    {
        return THERMO3_ERROR;
    }

    /* Read it back. If the sensor is not connected or the address is wrong,
     * we notice it here at the start and not somewhere later. */
    if (thermo3_read_reg(TMP102_REG_CONF, &high, &low) != THERMO3_OK)
    {
        return THERMO3_ERROR;
    }

    /* I only compare the bits we actually set. The AL bit (bit 5 of the low
     * byte) changes on its own depending on the temperature, so I mask it
     * out, otherwise the check would fail as soon as an alert is active. */
    if (high != TMP102_CONF_HIGH)
    {
        return THERMO3_ERROR;
    }

    if ((low & (uint8_t)~0x20u) != (TMP102_CONF_LOW & (uint8_t)~0x20u))
    {
        return THERMO3_ERROR;
    }

    return THERMO3_OK;
}

uint8_t thermo3_read_raw(int16_t *raw)
{
    uint8_t  high = 0;
    uint8_t  low  = 0;
    uint16_t value12;
    int16_t  result;

    if (raw == 0)
    {
        return THERMO3_ERROR;
    }

    if (thermo3_read_reg(TMP102_REG_TEMP, &high, &low) != THERMO3_OK)
    {
        return THERMO3_ERROR;
    }

    /* The temperature register is 16 bits but only the upper 12 bits are
     * used, the lower 4 bits are always zero ("left justified").
     * So I take the 8 bits of the high byte, shift them up by 4, and add
     * the upper 4 bits of the low byte. */
    value12 = (uint16_t)((uint16_t)high << 4) | (uint16_t)(low >> 4);

    result = (int16_t)value12;

    /* Now the value is a 12 bit two's complement number sitting in a 16 bit
     * variable. For negative temperatures bit 11 is set, and then the four
     * bits above it must be set as well, otherwise the compiler thinks it
     * is a big positive number instead of a small negative one.
     * (This is called sign extension - we had it in the lecture.) */
    if ((result & 0x0800) != 0)
    {
        result = (int16_t)(result | (int16_t)0xF000);
    }

    *raw = result;

    return THERMO3_OK;
}

int32_t thermo3_raw_to_centi_c(int16_t raw)
{
    /* One step of the raw value is 0.0625 degC. In hundredths of a degree
     * that is 6.25, and 6.25 = 625 / 100. So I first multiply and only then
     * divide, otherwise the result would always be rounded down to zero.
     * The multiplication is done in 32 bit, in 16 bit it would overflow. */
    return ((int32_t)raw * 625) / 100;
}

int32_t thermo3_centi_c_to_centi_f(int32_t centi_c)
{
    /* F = C * 9/5 + 32, and 32 degrees are 3200 hundredths.
     * Again multiply first, then divide. */
    return ((centi_c * 9) / 5) + 3200;
}

uint8_t thermo3_read_celsius(float *celsius)
{
    int16_t raw = 0;

    if (celsius == 0)
    {
        return THERMO3_ERROR;
    }

    if (thermo3_read_raw(&raw) != THERMO3_OK)
    {
        return THERMO3_ERROR;
    }

    *celsius = (float)raw * 0.0625f;

    return THERMO3_OK;
}

uint8_t thermo3_set_limits(int32_t low_centi_c, int32_t high_centi_c)
{
    int32_t raw_low;
    int32_t raw_high;
    uint16_t reg_low;
    uint16_t reg_high;

    /* A limit below the other one makes no sense and would break the
     * hysteresis, so I refuse it instead of writing nonsense to the sensor. */
    if (low_centi_c >= high_centi_c)
    {
        return THERMO3_ERROR;
    }

    /* Same conversion as above, only the other way round:
     * raw = centi / 6.25 = centi * 100 / 625 */
    raw_low  = (low_centi_c  * 100) / 625;
    raw_high = (high_centi_c * 100) / 625;

    /* TLOW and THIGH have the same left justified format as the temperature
     * register, so the 12 bits have to be shifted up by 4 again. */
    reg_low  = (uint16_t)((uint16_t)raw_low  << 4);
    reg_high = (uint16_t)((uint16_t)raw_high << 4);

    if (thermo3_write_reg(TMP102_REG_TLOW,
                          (uint8_t)(reg_low >> 8),
                          (uint8_t)(reg_low & 0xFFu)) != THERMO3_OK)
    {
        return THERMO3_ERROR;
    }

    if (thermo3_write_reg(TMP102_REG_THIGH,
                          (uint8_t)(reg_high >> 8),
                          (uint8_t)(reg_high & 0xFFu)) != THERMO3_OK)
    {
        return THERMO3_ERROR;
    }

    return THERMO3_OK;
}

void thermo3_set_alert_pin(GPIO_TypeDef *port, uint16_t pin)
{
    thermo3_alert_port = port;
    thermo3_alert_gpio = pin;
}

uint8_t thermo3_alert_active(void)
{
    uint8_t high = 0;
    uint8_t low  = 0;

    /* Case 1: the pin is wired and I know about it. This is the reliable
     * way, because the datasheet says it plainly: with POL = 0 the ALERT
     * pin is active LOW. So a low level means alert, no guessing. */
    if (thermo3_alert_port != 0)
    {
        if (HAL_GPIO_ReadPin(thermo3_alert_port, thermo3_alert_gpio)
                == GPIO_PIN_RESET)
        {
            return 1;
        }

        return 0;
    }

    /* Case 2: no pin registered, so I ask the sensor over I2C.
     * Bit 5 of the low byte is the AL flag. With POL = 0 it reads 1 while
     * everything is fine and changes to 0 when the alert becomes active,
     * so "alert" means the bit is NOT set. */
    if (thermo3_read_reg(TMP102_REG_CONF, &high, &low) != THERMO3_OK)
    {
        return 0;   /* if I cannot read it I say "no alert" */
    }

    if ((low & 0x20u) == 0)
    {
        return 1;
    }

    return 0;
}
