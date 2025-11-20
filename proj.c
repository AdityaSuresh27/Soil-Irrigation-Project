#include "stm32f4xx.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ======================= Configuration ======================= */
extern uint32_t SystemCoreClock;

#define SSD1306_I2C_ADDR      (0x3C << 1)
#define SSD1306_WIDTH         128
#define SSD1306_HEIGHT        64
#define SSD1306_BUFFER_SIZE   (SSD1306_WIDTH * SSD1306_HEIGHT / 8)

#define DHT11_PIN 3    // PA3
#define SOIL_PIN  5    // PA5 (digital)
#define LED_TEMP  0    // PB0
#define LED_WATER 2    // PB2
#define LED_SOIL  8    // PA8

#define TEMP_THRESHOLD         30
#define HUMIDITY_THRESHOLD_LOW 85

/* ======================= Globals ======================= */
static uint8_t SSD1306_Buffer[SSD1306_BUFFER_SIZE];
volatile uint32_t msTicks = 0;

uint8_t humidity_int, humidity_dec, temp_int, temp_dec, checksum;
uint8_t soil_dry = 0;
typedef enum { WATER_OFF, WATER_LOW, WATER_MED, WATER_HIGH } WaterLevel_t;
WaterLevel_t water_level = WATER_OFF;

/* ======================= Timing helpers (DWT) ======================= */
void SysTick_Handler(void) { msTicks++; }
void delay_ms(uint32_t ms) { uint32_t start = msTicks; while ((msTicks - start) < ms); }

static inline void DWT_Delay_Init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
static inline uint32_t micros(void) {
    return (uint32_t)(DWT->CYCCNT / (SystemCoreClock / 1000000U));
}
void delay_us(uint32_t us) {
    uint32_t start = micros();
    while ((micros() - start) < us) { __NOP(); }
}

/* ======================= SSD1306 low-level I2C ======================= */
void I2C1_Init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    GPIOB->MODER &= ~((3U << (6*2)) | (3U << (7*2)));
    GPIOB->MODER |=  ((2U << (6*2)) | (2U << (7*2)));
    GPIOB->OTYPER |= ((1U << 6) | (1U << 7));
    GPIOB->PUPDR &= ~((3U << (6*2)) | (3U << (7*2)));
    GPIOB->PUPDR |=  ((1U << (6*2)) | (1U << (7*2)));
    GPIOB->AFR[0] &= ~((0xF << (6*4)) | (0xF << (7*4)));
    GPIOB->AFR[0] |=  ((4U << (6*4)) | (4U << (7*4)));

    uint32_t pclk1_mhz = SystemCoreClock / 1000000U;
    I2C1->CR1 |= I2C_CR1_SWRST;
    I2C1->CR1 &= ~I2C_CR1_SWRST;
    I2C1->CR2 = (uint32_t)pclk1_mhz;

    uint32_t ccr = (pclk1_mhz * 1000000U) / (2U * 100000U);
    if (ccr == 0) ccr = 1;
    I2C1->CCR = (uint16_t)ccr;
    I2C1->TRISE = (uint8_t)(pclk1_mhz + 1U);
    I2C1->CR1 |= I2C_CR1_PE;
}

void I2C1_Write(uint8_t addr, uint8_t *data, size_t size) {
    while (I2C1->SR2 & I2C_SR2_BUSY) { __NOP(); }

    I2C1->CR1 |= I2C_CR1_START;
    while (!(I2C1->SR1 & I2C_SR1_SB)) { __NOP(); }
    I2C1->DR = addr;
    while (!(I2C1->SR1 & I2C_SR1_ADDR)) { __NOP(); }
    (void)I2C1->SR2;

    for (size_t i = 0; i < size; ++i) {
        while (!(I2C1->SR1 & I2C_SR1_TXE)) { __NOP(); }
        I2C1->DR = data[i];
    }
    while (!(I2C1->SR1 & I2C_SR1_BTF)) { __NOP(); }
    I2C1->CR1 |= I2C_CR1_STOP;
}

/* ======================= SSD1306 commands ======================= */
void ssd1306_WriteCommand(uint8_t cmd) {
    uint8_t buf[2] = { 0x00, cmd };
    I2C1_Write(SSD1306_I2C_ADDR, buf, 2);
}

void ssd1306_Init(void) {
    delay_ms(100);
    const uint8_t init_cmds[] = {
        0xAE,0x20,0x00,0xC8,0x40,0x81,0xFF,0xA1,0xA6,
        0xA8,0x3F,0xD3,0x00,0xD5,0xF0,0xD9,0x22,0xDA,
        0x12,0xDB,0x20,0x8D,0x14,0xAF
    };
    for (size_t i = 0; i < sizeof(init_cmds); ++i) ssd1306_WriteCommand(init_cmds[i]);
}

void ssd1306_UpdateScreen(void) {
    uint8_t packet[129];
    packet[0] = 0x40;
    for (uint8_t page = 0; page < 8; ++page) {
        ssd1306_WriteCommand(0xB0 + page);
        ssd1306_WriteCommand(0x00);
        ssd1306_WriteCommand(0x10);
        memcpy(packet + 1, &SSD1306_Buffer[SSD1306_WIDTH * page], 128);
        I2C1_Write(SSD1306_I2C_ADDR, packet, 129);
    }
}
void ssd1306_Clear(void) { memset(SSD1306_Buffer, 0, sizeof(SSD1306_Buffer)); }

void ssd1306_DrawPixel(uint8_t x, uint8_t y, uint8_t color) {
    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) return;
    uint16_t index = x + (y / 8) * SSD1306_WIDTH;
    if (color)
        SSD1306_Buffer[index] |= (1 << (y % 8));
    else
        SSD1306_Buffer[index] &= ~(1 << (y % 8));
}

/* ======================= Font + Character Drawing ======================= */
static const uint8_t Font5x7[95][5] = {
{0x00,0x00,0x00,0x00,0x00}, // ' ' 0x20
{0x00,0x00,0x5F,0x00,0x00}, // '!'
{0x00,0x07,0x00,0x07,0x00}, // '"'
{0x14,0x7F,0x14,0x7F,0x14}, // '#'
{0x24,0x2A,0x7F,0x2A,0x12}, // '$'
{0x23,0x13,0x08,0x64,0x62}, // '%'
{0x36,0x49,0x55,0x22,0x50}, // '&'
{0x00,0x05,0x03,0x00,0x00}, // '''
{0x00,0x1C,0x22,0x41,0x00}, // '('
{0x00,0x41,0x22,0x1C,0x00}, // ')'
{0x14,0x08,0x3E,0x08,0x14}, // '*'
{0x08,0x08,0x3E,0x08,0x08}, // '+'
{0x00,0x50,0x30,0x00,0x00}, // ','
{0x08,0x08,0x08,0x08,0x08}, // '-'
{0x00,0x60,0x60,0x00,0x00}, // '.'
{0x20,0x10,0x08,0x04,0x02}, // '/'
{0x3E,0x51,0x49,0x45,0x3E}, // '0'
{0x00,0x42,0x7F,0x40,0x00}, // '1'
{0x42,0x61,0x51,0x49,0x46}, // '2'
{0x21,0x41,0x45,0x4B,0x31}, // '3'
{0x18,0x14,0x12,0x7F,0x10}, // '4'
{0x27,0x45,0x45,0x45,0x39}, // '5'
{0x3C,0x4A,0x49,0x49,0x30}, // '6'
{0x01,0x71,0x09,0x05,0x03}, // '7'
{0x36,0x49,0x49,0x49,0x36}, // '8'
{0x06,0x49,0x49,0x29,0x1E}, // '9'
{0x00,0x36,0x36,0x00,0x00}, // ':'
{0x00,0x56,0x36,0x00,0x00}, // ';'
{0x08,0x14,0x22,0x41,0x00}, // '<'
{0x14,0x14,0x14,0x14,0x14}, // '='
{0x00,0x41,0x22,0x14,0x08}, // '>'
{0x02,0x01,0x51,0x09,0x06}, // '?'
{0x32,0x49,0x79,0x41,0x3E}, // '@'
{0x7E,0x11,0x11,0x11,0x7E}, // 'A'
{0x7F,0x49,0x49,0x49,0x36}, // 'B'
{0x3E,0x41,0x41,0x41,0x22}, // 'C'
{0x7F,0x41,0x41,0x22,0x1C}, // 'D'
{0x7F,0x49,0x49,0x49,0x41}, // 'E'
{0x7F,0x09,0x09,0x09,0x01}, // 'F'
{0x3E,0x41,0x49,0x49,0x7A}, // 'G'
{0x7F,0x08,0x08,0x08,0x7F}, // 'H'
{0x00,0x41,0x7F,0x41,0x00}, // 'I'
{0x20,0x40,0x41,0x3F,0x01}, // 'J'
{0x7F,0x08,0x14,0x22,0x41}, // 'K'
{0x7F,0x40,0x40,0x40,0x40}, // 'L'
{0x7F,0x02,0x0C,0x02,0x7F}, // 'M'
{0x7F,0x04,0x08,0x10,0x7F}, // 'N'
{0x3E,0x41,0x41,0x41,0x3E}, // 'O'
{0x7F,0x09,0x09,0x09,0x06}, // 'P'
{0x3E,0x41,0x51,0x21,0x5E}, // 'Q'
{0x7F,0x09,0x19,0x29,0x46}, // 'R'
{0x46,0x49,0x49,0x49,0x31}, // 'S'
{0x01,0x01,0x7F,0x01,0x01}, // 'T'
{0x3F,0x40,0x40,0x40,0x3F}, // 'U'
{0x1F,0x20,0x40,0x20,0x1F}, // 'V'
{0x3F,0x40,0x38,0x40,0x3F}, // 'W'
{0x63,0x14,0x08,0x14,0x63}, // 'X'
{0x07,0x08,0x70,0x08,0x07}, // 'Y'
{0x61,0x51,0x49,0x45,0x43}, // 'Z'
{0x00,0x7F,0x41,0x41,0x00}, // '['
{0x02,0x04,0x08,0x10,0x20}, // '\'
{0x00,0x41,0x41,0x7F,0x00}, // ']'
{0x04,0x02,0x01,0x02,0x04}, // '^'
{0x40,0x40,0x40,0x40,0x40}, // '_'
{0x00,0x01,0x02,0x04,0x00}, // '`'
{0x20,0x54,0x54,0x54,0x78}, // 'a'
{0x7F,0x48,0x44,0x44,0x38}, // 'b'
{0x38,0x44,0x44,0x44,0x20}, // 'c'
{0x38,0x44,0x44,0x48,0x7F}, // 'd'
{0x38,0x54,0x54,0x54,0x18}, // 'e'
{0x08,0x7E,0x09,0x01,0x02}, // 'f'
{0x0C,0x52,0x52,0x52,0x3E}, // 'g'
{0x7F,0x08,0x04,0x04,0x78}, // 'h'
{0x00,0x44,0x7D,0x40,0x00}, // 'i'
{0x20,0x40,0x44,0x3D,0x00}, // 'j'
{0x7F,0x10,0x28,0x44,0x00}, // 'k'
{0x00,0x41,0x7F,0x40,0x00}, // 'l'
{0x7C,0x04,0x18,0x04,0x78}, // 'm'
{0x7C,0x08,0x04,0x04,0x78}, // 'n'
{0x38,0x44,0x44,0x44,0x38}, // 'o'
{0x7C,0x14,0x14,0x14,0x08}, // 'p'
{0x08,0x14,0x14,0x18,0x7C}, // 'q'
{0x7C,0x08,0x04,0x04,0x08}, // 'r'
{0x48,0x54,0x54,0x54,0x20}, // 's'
{0x04,0x3F,0x44,0x40,0x20}, // 't'
{0x3C,0x40,0x40,0x20,0x7C}, // 'u'
{0x1C,0x20,0x40,0x20,0x1C}, // 'v'
{0x3C,0x40,0x30,0x40,0x3C}, // 'w'
{0x44,0x28,0x10,0x28,0x44}, // 'x'
{0x0C,0x50,0x50,0x50,0x3C}, // 'y'
{0x44,0x64,0x54,0x4C,0x44}, // 'z'
{0x00,0x08,0x36,0x41,0x00}, // '{'
{0x00,0x00,0x7F,0x00,0x00}, // '|'
{0x00,0x41,0x36,0x08,0x00}, // '}'
{0x10,0x08,0x08,0x10,0x08}  // '~'
};

void ssd1306_DrawCharScaled(uint8_t x, uint8_t y, char c, uint8_t scale) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *chr = Font5x7[c - 32];
    for (uint8_t col = 0; col < 5; ++col) {
        uint8_t column = chr[col];
        for (uint8_t row = 0; row < 7; ++row) {
            uint8_t pixel_on = (column >> row) & 0x01;
            if (pixel_on) {
                for (uint8_t sx = 0; sx < scale; ++sx) {
                    for (uint8_t sy = 0; sy < scale; ++sy) {
                        ssd1306_DrawPixel(x + col*scale + sx, y + row*scale + sy, 1);
                    }
                }
            }
        }
    }
}

void ssd1306_DrawStringScaled(uint8_t x, uint8_t y, const char *s, uint8_t scale) {
    while (*s) {
        if (x + (5 * scale) >= SSD1306_WIDTH) break;
        ssd1306_DrawCharScaled(x, y, *s++, scale);
        x += (6 * scale);
    }
}
void ssd1306_DrawString(uint8_t x, uint8_t y, const char *s) { ssd1306_DrawStringScaled(x,y,s,1); }

/* ======================= DHT11 + GPIO ======================= */
void DHT11_Pin_Output(void) { GPIOA->MODER &= ~(3U << (DHT11_PIN*2)); GPIOA->MODER |=  (1U << (DHT11_PIN*2)); }
void DHT11_Pin_Input(void)  { GPIOA->MODER &= ~(3U << (DHT11_PIN*2)); }

uint8_t DHT11_ReadBit(void) {
    while (!(GPIOA->IDR & (1U << DHT11_PIN)));
    delay_us(40);
    uint8_t bit = (GPIOA->IDR & (1U << DHT11_PIN)) ? 1 : 0;
    while (GPIOA->IDR & (1U << DHT11_PIN));
    return bit;
}

uint8_t DHT11_ReadByte(void) {
    uint8_t data = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        data <<= 1;
        if (DHT11_ReadBit()) data |= 1;
    }
    return data;
}

uint8_t DHT11_ReadData(void) {
    DHT11_Pin_Output();
    GPIOA->ODR &= ~(1U << DHT11_PIN);
    delay_ms(20);
    GPIOA->ODR |= (1U << DHT11_PIN);
    delay_us(30);
    DHT11_Pin_Input();

    delay_us(40);
    if (GPIOA->IDR & (1U << DHT11_PIN)) return 1;

    while (!(GPIOA->IDR & (1U << DHT11_PIN)));
    while (GPIOA->IDR & (1U << DHT11_PIN));

    humidity_int = DHT11_ReadByte();
    humidity_dec = DHT11_ReadByte();
    temp_int     = DHT11_ReadByte();
    temp_dec     = DHT11_ReadByte();
    checksum     = DHT11_ReadByte();

    if (checksum != (uint8_t)(humidity_int + humidity_dec + temp_int + temp_dec)) return 2;
    return 0;
}

void GPIO_Init_All(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;
    DHT11_Pin_Input();

    GPIOA->MODER &= ~(3U << (SOIL_PIN*2));
    GPIOA->PUPDR &= ~(3U << (SOIL_PIN*2));
    GPIOA->PUPDR |=  (1U << (SOIL_PIN*2));

    GPIOB->MODER &= ~((3U << (LED_TEMP*2)) | (3U << (LED_WATER*2)));
    GPIOB->MODER |=  ((1U << (LED_TEMP*2)) | (1U << (LED_WATER*2)));
    GPIOA->MODER &= ~(3U << (LED_SOIL*2));
    GPIOA->MODER |=  (1U << (LED_SOIL*2));

    GPIOB->ODR &= ~((1U<<LED_TEMP) | (1U<<LED_WATER));
    GPIOA->ODR &= ~(1U<<LED_SOIL);
}

/* ======================= Display layout ======================= */
void drawWaterBar(uint8_t x, uint8_t y, WaterLevel_t lvl) {
    const uint8_t segW = 14;
    const uint8_t segH = 8;
    for (uint8_t i = 0; i < 3; ++i) {
        uint8_t segx = x + i * (segW + 2);
        for (uint8_t xx = 0; xx < segW; ++xx) {
            ssd1306_DrawPixel(segx + xx, y, 1);
            ssd1306_DrawPixel(segx + xx, y + segH - 1, 1);
        }
        for (uint8_t yy = 0; yy < segH; ++yy) {
            ssd1306_DrawPixel(segx, y + yy, 1);
            ssd1306_DrawPixel(segx + segW - 1, y + yy, 1);
        }
    }
    if (lvl == WATER_HIGH) {
        for (uint8_t i = 0; i < 3; ++i)
            for (uint8_t xx = 2; xx < segW-2; ++xx)
                for (uint8_t yy = 2; yy < segH-2; ++yy)
                    ssd1306_DrawPixel(x + i*(segW+2) + xx, y + yy, 1);
    } else if (lvl == WATER_MED) {
        for (uint8_t i = 0; i < 2; ++i)
            for (uint8_t xx = 2; xx < segW-2; ++xx)
                for (uint8_t yy = 2; yy < segH-2; ++yy)
                    ssd1306_DrawPixel(x + i*(segW+2) + xx, y + yy, 1);
    } else if (lvl == WATER_LOW) {
        uint8_t i = 0;
        for (uint8_t xx = 2; xx < segW-2; ++xx)
            for (uint8_t yy = 2; yy < segH-2; ++yy)
                ssd1306_DrawPixel(x + i*(segW+2) + xx, y + yy, 1);
    }
}

/* ======================= Updated Display with soil moved to bottom ======================= */
void Display_Update(void) {
    char buf[32];
    ssd1306_Clear();

    snprintf(buf, sizeof(buf), "%dC", temp_int);
    ssd1306_DrawStringScaled(0, 0, buf, 3);

    snprintf(buf, sizeof(buf), "Hum: %d%%", humidity_int);
    ssd1306_DrawStringScaled(0, 22, buf, 2);

    const char *wtxt;
    switch (water_level) {
        case WATER_OFF: wtxt = "Water: OFF"; break;
        case WATER_LOW: wtxt = "Water: LOW"; break;
        case WATER_MED: wtxt = "Water: MED"; break;
        default:        wtxt = "Water: HIGH"; break;
    }
    ssd1306_DrawStringScaled(0, 44, wtxt, 1);
    drawWaterBar(64, 44, water_level);

    /* Moved Soil state to bottom */
    snprintf(buf, sizeof(buf), "Soil: %s", soil_dry ? "DRY" : "WET");
    ssd1306_DrawStringScaled(0, 56, buf, 1);

    ssd1306_UpdateScreen();
}

/* ======================= Main Loop ======================= */
int main(void) {
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000U);
    DWT_Delay_Init();

    GPIO_Init_All();
    I2C1_Init();
    ssd1306_Init();

    ssd1306_Clear();
    ssd1306_DrawStringScaled(10, 20, "SMART FARM", 2);
    ssd1306_UpdateScreen();
    delay_ms(600);

    while (1) {
        uint8_t st = DHT11_ReadData();
        soil_dry = ((GPIOA->IDR & (1U << SOIL_PIN)) ? 0 : 1);

        bool hot = false;
        bool dry_air = false;

        if (st == 0) {
            hot = (temp_int >= TEMP_THRESHOLD);
            dry_air = (humidity_int <= HUMIDITY_THRESHOLD_LOW);

            if (hot && dry_air) water_level = soil_dry ? WATER_HIGH : WATER_OFF;
            else if (hot || dry_air) water_level = soil_dry ? WATER_MED : WATER_OFF;
            else water_level = soil_dry ? WATER_LOW : WATER_OFF;
        } else {
            /* sensor error: don't change water_level or set to OFF */
            water_level = WATER_OFF;
        }

        /* === LED logic per your specification ===
           LED_TEMP (PB0):
             - If temp>threshold OR humidity< threshold -> LED PB0 ON
             - If BOTH (temp>threshold AND humidity< threshold) -> blink (indicate both)
             - Else -> OFF

           LED_WATER (PB2):
             Only active if soil is dry:
             - soil_dry && both(temp> & hum<)  -> FAST blink (max water)
             - soil_dry && (temp> OR hum<)    -> SLOW blink (medium water)
             - soil_dry && neither             -> STEADY ON (some water)
             - else -> OFF
        */
        uint32_t t = msTicks;

        /* LED_TEMP (PB0) handling — blink faster */
        if (hot && dry_air) {
            /* both present -> faster blink (period 150 ms, toggle every 75 ms) */
            if ((t % 150) < 75) {
                GPIOB->ODR |= (1U << LED_TEMP);
            } else {
                GPIOB->ODR &= ~(1U << LED_TEMP);
            }
        } else if (hot || dry_air) {
            /* any one -> steady ON */
            GPIOB->ODR |= (1U << LED_TEMP);
        } else {
            /* off */
            GPIOB->ODR &= ~(1U << LED_TEMP);
        }

        /* LED_WATER (PB2) handling — all blink rates faster */
        if (soil_dry) {
            if (hot && dry_air) {
                /* FAST blink -> max water (toggle every 50 ms -> period 100 ms) */
                if ((t % 100) < 50) GPIOB->ODR |= (1U << LED_WATER);
                else GPIOB->ODR &= ~(1U << LED_WATER);
            } else if (hot || dry_air) {
                /* SLOW blink -> medium water (toggle every 250 ms -> period 500 ms) */
                if ((t % 500) < 250) GPIOB->ODR |= (1U << LED_WATER);
                else GPIOB->ODR &= ~(1U << LED_WATER);
            } else {
                /* soil dry but neither temp/humidity extreme -> steady ON (some water) */
                GPIOB->ODR |= (1U << LED_WATER);
            }
        } else {
            /* soil wet -> LED_WATER OFF */
            GPIOB->ODR &= ~(1U << LED_WATER);
        }

        /* LED_SOIL (PA8) - unchanged: on when soil_dry */
        if (soil_dry) GPIOA->ODR |= (1U << LED_SOIL);
        else GPIOA->ODR &= ~(1U << LED_SOIL);


        Display_Update();
        delay_ms(1000);
    }
    return 0;
}