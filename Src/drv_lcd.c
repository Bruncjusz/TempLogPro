/*
 * Segment LCD driver for the TempTale Ultra glass (STM32L1 LCD peripheral,
 * 4 COM, 1/4 duty). Segments are addressed as (RAM word, bit) exactly as in
 * the "TempTale Ultra LCD RAM Map" (temptale-ultra_LCD.pdf); the table in
 * docs/claude_handover_2026-10-09.md lists the whole map.
 */
#include "board.h"
#include "board_regs.h"
#include "templogpro_hw_bridge.h"

/* Only the even RAM words are used by this glass. */
#define RAM0  0u
#define RAM2  2u
#define RAM4  4u
#define RAM6  6u

/* Each 7-segment digit is spread across 4 RAM words: segments A-D share one
 * bit in RAM0/RAM2/RAM4/RAM6, segments E-G share another in RAM0/RAM2/RAM4. */
typedef struct {
    uint8_t bit_ad; /* bit of segments A,B,C,D (RAM0,RAM2,RAM4,RAM6) */
    uint8_t bit_eg; /* bit of segments E,F,G   (RAM0,RAM2,RAM4) */
} DigitBits;

/* Digit field "1 . 8 : 8 . 8": pos0 hundreds, pos1 tens, pos2 units. */
static const DigitBits DIGIT_BIT_POS[3] = {
    { 24u, 15u },
    { 12u,  6u },
    {  5u, 23u },
};

#define SEG_HEART            RAM0, 26u
#define SEG_NEGATIVE         RAM2, 26u
#define SEG_DEGREE_SYMBOL    RAM0,  2u  /* "°" */
#define SEG_DEGREE_C         RAM4,  2u  /* "C", together with the above "°C" */
#define SEG_DOT_DECIMAL      RAM6,  6u  /* right dot, between 2nd and 3rd digit: "30.1" */
#define SEG_PERCENT          RAM0,  1u
#define SEG_RH               RAM2,  1u
#define SEG_HRS              RAM4,  1u
#define SEG_MINS             RAM6,  1u
#define SEG_CHECK_MARK       RAM0, 13u
#define SEG_BIG_ARROW        RAM0, 14u
#define SEG_UP_ARROW         RAM0, 22u
#define SEG_SUN              RAM0, 27u
#define SEG_BACKSLASH        RAM2, 13u
#define SEG_TEMP             RAM2, 14u
#define SEG_DOWN_ARROW       RAM2, 22u
#define SEG_PROBE            RAM2, 27u
#define SEG_SLASH            RAM4, 13u
#define SEG_COLON            RAM4, 14u  /* between hundreds and tens digit */
#define SEG_STOP_SIGN        RAM4, 22u
#define SEG_DAYS             RAM4, 25u
#define SEG_BATTERY          RAM4, 26u
#define SEG_HOURGLASS        RAM4, 27u
#define SEG_AVG              RAM6, 13u
#define SEG_MIN              RAM6, 23u
#define SEG_MAX              RAM6, 22u
#define SEG_MID_DOT          RAM6, 15u
#define SEG_TIME             RAM6, 26u
#define SEG_DEGREE_F         RAM2,  2u  /* "F", together with "°" gives "°F" */
#define SEG_DIGIT_1000       RAM0, 25u  /* leading "1": temperatures 100.0..199.9 */
/* Remaining segment of the map, unused so far. */
#define SEG_LEFT_DOT         RAM2, 25u

/*
 * LCD_CR duty/bias: the map uses all four COM lines (RAM0/2/4/6), so 1/4 duty
 * is required. 1/3 duty stops refreshing RAM6 (COM3) and loses a quarter of
 * the segments, including the bottom bar of every digit.
 */
#define LCD_CR_DUTY_BIAS     (0x0000000Cu | 0x00000040u) /* 4 COM, 1/4 duty, 1/3 bias */
#define LCD_CR_MUX           0x00000080u
#define LCD_CR_ENABLE        0x00000001u
#define LCD_CR_VSEL_INTERNAL 0x00000000u /* confirmed on this board */

#define LCD_FCR_DIV31         0x003C0000u
#define LCD_FCR_PS(n)         (((uint32_t)(n) & 0xFu) << 22)
#define LCD_FCR_CONTRAST(n)   (((uint32_t)(n) & 0x7u) << 10)
#define LCD_FCR_DEAD(n)       (((uint32_t)(n) & 0x7u) << 7)
#define LCD_FCR_PON(n)        (((uint32_t)(n) & 0x7u) << 4)

#define LCD_RCC_CSR_LSION     (1u << 0)
#define LCD_RCC_CSR_LSIRDY    (1u << 1)
#define LCD_RCC_CSR_LSEON     (1u << 8)
#define LCD_RCC_CSR_LSERDY    (1u << 9)
#define LCD_RCC_CSR_RTCSEL    (3u << 16)
#define LCD_RCC_CSR_RTCSEL_LSE (1u << 16)
#define LCD_RCC_CSR_RTCSEL_LSI (2u << 16)
#define LCD_RCC_CSR_RTCEN     (1u << 22)
#define LCD_RCC_CSR_RTCRST    (1u << 23)

#define LCD_CLOCK_TIMEOUT_MS  100u
#define LCD_READY_TIMEOUT_MS  1000u

static const uint8_t SEVENSEG_DIGITS[10] = {
    0x3Fu, 0x06u, 0x5Bu, 0x4Fu, 0x66u,
    0x6Du, 0x7Du, 0x07u, 0x7Fu, 0x6Fu,
};

static uint32_t g_lcd_icon_mask;

/* All LCD segment pins, leaving buttons, SPI1, USB, SWD, LEDs and LSE alone. */
static void init_lcd_pins_bruteforce(void)
{
    RCC_AHBENR |= 0x0000001Fu; /* GPIOA-GPIOE */

    board_gpio_af(GPIOA_BASE, 0x9FFFu & ~((1u << 0) | (1u << 1) |
                                          (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7) |
                                          (1u << 11) | (1u << 12) |
                                          (1u << 13) | (1u << 14)), BOARD_AF11_LCD);
    board_gpio_af(GPIOB_BASE, 0xFFFFu & ~(1u << LED_RED_PIN), BOARD_AF11_LCD);
    board_gpio_af(GPIOC_BASE, 0xFFFEu & ~((1u << 14) | (1u << 15)), BOARD_AF11_LCD);
    board_gpio_af(GPIOD_BASE, 0xFFFFu, BOARD_AF11_LCD);
    board_gpio_af(GPIOE_BASE, 0xFFFFu, BOARD_AF11_LCD);
}

static void lcd_set(uint32_t ram, uint32_t bit)
{
    LCD_RAM(ram) |= (1u << bit);
}

static void lcd_clear_ram(void)
{
    uint32_t start_ms;

    if ((LCD_CR & 1u) != 0u) {
        start_ms = HAL_GetTick();
        while ((LCD_SR & LCD_SR_UDR_BIT) != 0u) {
            if ((HAL_GetTick() - start_ms) >= 1000u) {
                break;
            }
        }
    }

    for (uint32_t i = 0; i < 16; i++) {
        LCD_RAM(i) = 0;
    }
}

static void lcd_commit(void)
{
    LCD_CLR = LCD_SR_UDD_BIT;
    LCD_SR |= LCD_SR_UDR_BIT;
}

static void lcd_apply_icons(void)
{
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_HEART) != 0u) {
        lcd_set(SEG_HEART);
    }
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_DEGREE_C) != 0u) {
        lcd_set(SEG_DEGREE_SYMBOL);
        lcd_set(SEG_DEGREE_C);
    }
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_DEGREE_F) != 0u) {
        lcd_set(SEG_DEGREE_SYMBOL);
        lcd_set(SEG_DEGREE_F);
    }
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_PERCENT) != 0u) lcd_set(SEG_PERCENT);
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_RH) != 0u) lcd_set(SEG_RH);
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_HRS) != 0u) lcd_set(SEG_HRS);
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_MINS) != 0u) lcd_set(SEG_MINS);
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_DAYS) != 0u) lcd_set(SEG_DAYS);
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_TEMP) != 0u) lcd_set(SEG_TEMP);
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_PROBE) != 0u) lcd_set(SEG_PROBE);
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_AVG) != 0u) lcd_set(SEG_AVG);
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_MAX) != 0u) lcd_set(SEG_MAX);
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_MIN) != 0u) lcd_set(SEG_MIN);
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_TIME) != 0u) lcd_set(SEG_TIME);
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_BATTERY) != 0u) lcd_set(SEG_BATTERY);
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_HOURGLASS) != 0u) lcd_set(SEG_HOURGLASS);
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_STOP) != 0u) lcd_set(SEG_STOP_SIGN);
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_CHECK) != 0u) {
        lcd_set(SEG_CHECK_MARK);
        lcd_set(SEG_SLASH);
    }
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_BIG_ARROW) != 0u) lcd_set(SEG_BIG_ARROW);
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_UP) != 0u) lcd_set(SEG_UP_ARROW);
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_DOWN) != 0u) lcd_set(SEG_DOWN_ARROW);
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_X) != 0u) {
        lcd_set(SEG_SLASH);
        lcd_set(SEG_BACKSLASH);
    }
    if ((g_lcd_icon_mask & TEMPLOGPRO_LCD_ICON_SUN) != 0u) lcd_set(SEG_SUN);
}

/* Draws a 7-segment pattern (bit0=A .. bit6=G) at a digit position. */
static void lcd_draw_segments(uint32_t pos, uint8_t pattern)
{
    uint8_t bit_ad = DIGIT_BIT_POS[pos].bit_ad;
    uint8_t bit_eg = DIGIT_BIT_POS[pos].bit_eg;

    if (pattern & (1u << 0)) lcd_set(RAM0, bit_ad); /* A */
    if (pattern & (1u << 1)) lcd_set(RAM2, bit_ad); /* B */
    if (pattern & (1u << 2)) lcd_set(RAM4, bit_ad); /* C */
    if (pattern & (1u << 3)) lcd_set(RAM6, bit_ad); /* D */
    if (pattern & (1u << 4)) lcd_set(RAM0, bit_eg); /* E */
    if (pattern & (1u << 5)) lcd_set(RAM2, bit_eg); /* F */
    if (pattern & (1u << 6)) lcd_set(RAM4, bit_eg); /* G */
}

static void lcd_draw_digit(uint32_t pos, uint32_t value)
{
    lcd_draw_segments(pos, SEVENSEG_DIGITS[value % 10u]);
}

static uint32_t lcd_wait_register(volatile uint32_t *reg, uint32_t mask,
                                  uint32_t expected, uint32_t timeout_ms)
{
    uint32_t start_ms = HAL_GetTick();

    while ((*reg & mask) != expected) {
        if ((HAL_GetTick() - start_ms) >= timeout_ms) {
            return 0u;
        }
    }
    return 1u;
}

static uint32_t lcd_prepare_clock(void)
{
    uint32_t rtcsel;

    RCC_APB1ENR |= RCC_APB1ENR_PWREN;
    PWR_CR |= PWR_CR_DBP;       /* backup-domain write enable */
    if (lcd_wait_register(&PWR_CR, PWR_CR_DBP, PWR_CR_DBP, LCD_CLOCK_TIMEOUT_MS) == 0u) {
        return 0u;
    }

    rtcsel = RCC_CSR & LCD_RCC_CSR_RTCSEL;
    if (((rtcsel == LCD_RCC_CSR_RTCSEL_LSE) &&
         ((RCC_CSR & LCD_RCC_CSR_LSERDY) != 0u)) ||
        ((rtcsel == LCD_RCC_CSR_RTCSEL_LSI) &&
         ((RCC_CSR & LCD_RCC_CSR_LSIRDY) != 0u))) {
        RCC_CSR |= LCD_RCC_CSR_RTCEN;
        return 1u;
    }

    /* Do not block the screen while LSE starts: the LCD runs from LSI right
       away and the RTC module moves the domain to LSE later. */
    RCC_CSR |= LCD_RCC_CSR_LSION;
    if (lcd_wait_register(&RCC_CSR, LCD_RCC_CSR_LSIRDY,
                          LCD_RCC_CSR_LSIRDY, LCD_CLOCK_TIMEOUT_MS) == 0u) {
        return 0u;
    }

    if ((rtcsel != LCD_RCC_CSR_RTCSEL_LSI) ||
        ((RCC_CSR & LCD_RCC_CSR_RTCEN) == 0u)) {
        RCC_CSR &= ~LCD_RCC_CSR_RTCEN;
        if (rtcsel != 0u) {
            RCC_CSR |= LCD_RCC_CSR_RTCRST;
            RCC_CSR &= ~LCD_RCC_CSR_RTCRST;
        }
        RCC_CSR &= ~LCD_RCC_CSR_RTCSEL;
        RCC_CSR |= LCD_RCC_CSR_RTCSEL_LSI;
        RCC_CSR |= LCD_RCC_CSR_RTCEN;
    }

    return 1u;
}

static void init_lcd(void)
{
    if (lcd_prepare_clock() == 0u) {
        return;
    }

    RCC_APB1ENR |= RCC_APB1ENR_LCDEN;

    LCD_CR &= ~LCD_CR_ENABLE;
    lcd_clear_ram();
    LCD_CLR = LCD_SR_UDD_BIT;
    LCD_SR |= LCD_SR_UDR_BIT;

    /* Brightness verified on this glass: DEAD=0, PON=5. PS=2 divides the
       LSI-clocked frame rate from ~300 Hz to ~75 Hz; PON scales with the
       prescaler, so the drive pulse keeps the same share of each phase. */
    LCD_FCR = LCD_FCR_PS(2) | LCD_FCR_DIV31 | LCD_FCR_CONTRAST(3) | LCD_FCR_DEAD(0) | LCD_FCR_PON(5);
    if (lcd_wait_register(&LCD_SR, LCD_SR_FCRSF_BIT,
                          LCD_SR_FCRSF_BIT, LCD_READY_TIMEOUT_MS) == 0u) {
        return;
    }

    LCD_CR = LCD_CR_DUTY_BIAS | LCD_CR_MUX | LCD_CR_VSEL_INTERNAL;
    LCD_CR |= LCD_CR_ENABLE;

    if (lcd_wait_register(&LCD_SR, LCD_SR_ENS_BIT,
                          LCD_SR_ENS_BIT, LCD_READY_TIMEOUT_MS) == 0u) {
        return;
    }
    (void)lcd_wait_register(&LCD_SR, LCD_SR_RDY_BIT,
                            LCD_SR_RDY_BIT, LCD_READY_TIMEOUT_MS);
}

/* "XX.X" with the real decimal dot and minus sign, up to 199.9 using the
   leading "1". The unit (°C/°F) comes from the icon mask; heart = heartbeat. */
static void lcd_show_temperature(int32_t temp_x256, uint32_t heart_on)
{
    uint32_t negative = (temp_x256 < 0) ? 1u : 0u;
    int32_t abs_x256 = negative ? -temp_x256 : temp_x256;

    int32_t tenths = (abs_x256 * 10 + 128) / 256; /* rounded */
    if (tenths > 1999) {
        tenths = 1999;
    }
    uint32_t degrees = (uint32_t)(tenths / 10);
    uint32_t frac    = (uint32_t)(tenths % 10);

    lcd_clear_ram();

    if (degrees >= 100u) {
        lcd_set(SEG_DIGIT_1000);
        degrees -= 100u;
    }
    lcd_draw_digit(0, degrees / 10u);
    lcd_draw_digit(1, degrees % 10u);
    lcd_draw_digit(2, frac);

    lcd_set(SEG_DOT_DECIMAL);

    if (negative) {
        lcd_set(SEG_NEGATIVE);
    }
    if (heart_on) {
        lcd_set(SEG_HEART);
    }

    lcd_apply_icons();
    lcd_commit();
}

static void lcd_show_value(uint16_t value)
{
    uint32_t shown = (uint32_t)(value % 1000u);

    lcd_clear_ram();
    if (shown >= 100u) lcd_draw_digit(0, shown / 100u);
    if (shown >= 10u) lcd_draw_digit(1, (shown / 10u) % 10u);
    lcd_draw_digit(2, shown % 10u);
    lcd_apply_icons();
    lcd_commit();
}

static void lcd_show_vdd_mv(uint16_t millivolts)
{
    uint32_t centivolts = ((uint32_t)millivolts + 5u) / 10u;

    if (centivolts > 999u) {
        centivolts = 999u;
    }

    lcd_clear_ram();
    lcd_draw_digit(0, centivolts / 100u);
    lcd_draw_digit(1, (centivolts / 10u) % 10u);
    lcd_draw_digit(2, centivolts % 10u);
    lcd_apply_icons();
    lcd_commit();
}

static uint8_t lcd_pattern_for_char(char ch)
{
    if ((ch >= '0') && (ch <= '9')) {
        return SEVENSEG_DIGITS[(uint32_t)(ch - '0')];
    }

    switch (ch) {
    case 'A': case 'a': return 0x77u;
    case 'B': case 'b': return 0x7Cu;
    case 'C': return 0x39u;
    case 'c': return 0x58u;
    case 'D': case 'd': return 0x5Eu;
    case 'E': case 'e': return 0x79u;
    case 'F': case 'f': return 0x71u;
    case 'G': case 'g': return 0x3Du;
    case 'H': case 'h': return 0x76u;
    case 'I': case 'i': return 0x06u;
    case 'L': case 'l': return 0x38u;
    case 'N': case 'n': return 0x54u;
    case 'O': case 'o': return 0x5Cu;
    case 'P': case 'p': return 0x73u;
    case 'R': case 'r': return 0x50u;
    case 'S': case 's': return 0x6Du;
    case 'T': case 't': return 0x78u;
    case 'U': case 'u': return 0x3Eu;
    case 'Y': case 'y': return 0x6Eu;
    case '-': return 0x40u;
    default: return 0x00u;
    }
}

static void lcd_show_text3(const char text[3])
{
    lcd_clear_ram();
    lcd_draw_segments(0, lcd_pattern_for_char(text[0]));
    lcd_draw_segments(1, lcd_pattern_for_char(text[1]));
    lcd_draw_segments(2, lcd_pattern_for_char(text[2]));
    lcd_apply_icons();
    lcd_commit();
}

/* ---- single-segment probe (RAM-only test in templogpro_app.c) ---- */

static uint32_t lcd_scan_is_digit_segment(uint32_t ram, uint32_t bit)
{
    for (uint32_t pos = 0; pos < 3u; pos++) {
        if (bit == DIGIT_BIT_POS[pos].bit_ad) {
            return 1u;
        }
        if ((ram != RAM6) && (bit == DIGIT_BIT_POS[pos].bit_eg)) {
            return 1u;
        }
    }
    return 0u;
}

static uint32_t lcd_scan_candidate_for_index(uint32_t index, uint32_t *out_ram, uint32_t *out_bit)
{
    uint32_t seen = 0u;

    for (uint32_t ram = RAM0; ram <= RAM6; ram += 2u) {
        for (uint32_t bit = 0u; bit < 32u; bit++) {
            if (lcd_scan_is_digit_segment(ram, bit) != 0u) {
                continue;
            }
            if (seen == index) {
                *out_ram = ram;
                *out_bit = bit;
                return 1u;
            }
            seen++;
        }
    }
    return 0u;
}

static uint32_t lcd_scan_candidate_count(void)
{
    uint32_t count = 0u;

    for (uint32_t ram = RAM0; ram <= RAM6; ram += 2u) {
        for (uint32_t bit = 0u; bit < 32u; bit++) {
            if (lcd_scan_is_digit_segment(ram, bit) == 0u) {
                count++;
            }
        }
    }
    return count;
}

static void lcd_show_icon_scan(uint32_t index)
{
    uint32_t ram;
    uint32_t bit;
    uint32_t count = lcd_scan_candidate_count();

    lcd_clear_ram();
    if (count == 0u) {
        lcd_draw_segments(0, lcd_pattern_for_char('n'));
        lcd_draw_segments(1, lcd_pattern_for_char('o'));
        lcd_draw_segments(2, lcd_pattern_for_char('n'));
        lcd_commit();
        return;
    }

    index %= count;
    lcd_draw_digit(0, index / 100u);
    lcd_draw_digit(1, (index / 10u) % 10u);
    lcd_draw_digit(2, index % 10u);

    if (lcd_scan_candidate_for_index(index, &ram, &bit) != 0u) {
        lcd_set(ram, bit);
    }
    lcd_commit();
}

/* ---- bridge for the application ---- */

void templogpro_hw_lcd_init(void)
{
    init_lcd_pins_bruteforce();
    init_lcd();
}

void templogpro_hw_lcd_suspend(void)
{
    uint32_t timeout = 100000u;

    LCD_CR = 0u;
    while (((LCD_SR & 1u) != 0u) && (timeout != 0u)) {
        timeout--;
    }
}

void templogpro_hw_lcd_show_clock12(uint32_t hours, uint32_t minutes, uint32_t colon_on)
{
    lcd_clear_ram();
    if (hours >= 10u) {
        lcd_set(SEG_DIGIT_1000);
        hours -= 10u;
    }
    lcd_draw_digit(0, hours % 10u);
    lcd_draw_digit(1, (minutes / 10u) % 10u);
    lcd_draw_digit(2, minutes % 10u);
    if (colon_on != 0u) {
        lcd_set(SEG_COLON);
    }
    lcd_apply_icons();
    lcd_commit();
}

void templogpro_hw_lcd_show_value(uint16_t value)
{
    lcd_show_value(value);
}

void templogpro_hw_lcd_show_text3(const char text[3])
{
    lcd_show_text3(text);
}

void templogpro_hw_lcd_show_temperature_x256(int32_t temperature_x256, uint32_t heart_on)
{
    lcd_show_temperature(temperature_x256, heart_on);
}

void templogpro_hw_lcd_show_vdd_mv(uint16_t millivolts)
{
    lcd_show_vdd_mv(millivolts);
}

void templogpro_hw_lcd_set_icon_mask(uint32_t icon_mask)
{
    g_lcd_icon_mask = icon_mask;
}

void templogpro_hw_lcd_show_icon_scan(uint32_t index)
{
    lcd_show_icon_scan(index);
}

uint32_t templogpro_hw_lcd_icon_scan_count(void)
{
    return lcd_scan_candidate_count();
}
