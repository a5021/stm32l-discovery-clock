/**
 * @file    main.c
 * @brief   STM32L152RB clock with LCD, RTC, and PC sync via RAM mailbox
 *
 * The PC writes seconds since midnight to address 0x20000000 using ST‑Link.
 * The firmware reads that value, updates the RTC, and displays time on LCD.
 */

#include "stm32l152xb.h"
#include "core_cm3.h"

/* HardFault handler – infinite loop */
void HardFault_Handler(void) { while (1); }

/* ====================================================================
 * Mailbox for ST‑Link (fixed RAM address)
 * ====================================================================
 * The PC writes a 32‑bit value here: seconds since midnight.
 * The firmware polls this location and updates the RTC when it sees
 * a value different from 0xFFFFFFFF (the "empty" marker).
 */
#define pc_sync_time (*(volatile uint32_t*)0x20000000)

/* ====================================================================
 * LCD font – official STM32 BSP for the MB963B board
 * ====================================================================
 * Each 16‑bit value encodes the segments for one digit (0‑9)
 * across the 4 common lines (COM0..COM3).
 */
static const uint16_t NumberMap[10] = {
    0x5F00, 0x4200, 0xF500, 0x6700, 0xEA00,
    0xAF00, 0xBF00, 0x4600, 0xFF00, 0xEF00
};

/* ====================================================================
 * LCD hardware mapping
 * ====================================================================
 * seg_shift[]       : maps a logical segment index to its bit position
 *                     in the LCD RAM register.
 * seg_glass[6][4]   : each of the 6 digit positions uses 4 segment indices.
 * com_ram[4]        : maps logical common line (0..3) to physical LCD RAM
 *                     register number (0,2,4,6).
 */
static const uint8_t seg_shift[24] = {
    0, 1, 2, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    17, 16, 18, 19, 20, 21, 24, 25, 26, 27, 28, 29
};
static const uint8_t seg_glass[6][4] = {
    {0,  1,  22, 23},
    {2,  3,  20, 21},
    {4,  5,  18, 19},
    {6,  7,  16, 17},
    {8,  9,  14, 15},
    {10, 11, 12, 13},
};
static const uint8_t com_ram[4] = {0, 2, 4, 6};

/* Bit masks for the two colon dots on the LCD */
#define COLON_HH_MM_MASK  (1u << 7)
#define COLON_MM_SS_MASK  (1u << 11)

/**
 * @brief Wait for the LCD to become ready and start an update.
 */
static void lcd_udr(void) {
    while (LCD->SR & LCD_SR_UDR);               // Wait until not busy
    LCD->CLR = LCD_SR_UDD_Msk;                  // Clear update done flag
    __DSB();
    LCD->SR |= LCD_SR_UDR;                      // Request update
    __DSB();
    while (!(LCD->SR & LCD_SR_UDD_Msk));        // Wait for update to finish
}

/**
 * @brief Write a 16‑bit code (font pattern) to a specific digit position.
 * @param pos Digit index 0..5 (0=leftmost, 5=rightmost)
 * @param code 16‑bit value from NumberMap (or 0 to clear)
 */
static void lcd_write_pos(int pos, uint16_t code) {
    if (pos < 0 || pos > 5) return;

    // Extract the four 4‑bit nibbles (each nibble drives one COM line)
    uint8_t nibble[4];
    nibble[0] = (code >> 12) & 0x0F;
    nibble[1] = (code >> 8)  & 0x0F;
    nibble[2] = (code >> 4)  & 0x0F;
    nibble[3] = (code >> 0)  & 0x0F;

    uint32_t mask = 0, data[4] = {0,0,0,0};

    // For each of the 4 segments in this digit, find its bit position
    for (int b = 0; b < 4; b++) {
        int sh = seg_shift[seg_glass[pos][b]];
        mask |= (1u << sh);                     // Mark this bit as used
        for (int c = 0; c < 4; c++) {          // For each COM line
            if ((nibble[c] >> b) & 1)
                data[c] |= (1u << sh);          // Turn on this segment
        }
    }

    // Write the new pattern to the four LCD RAM registers (COM0..COM3)
    for (int c = 0; c < 4; c++) {
        uint32_t m = mask;
        // Protect colon bits (they are on COM2 and COM3) – do not erase them
        if (c == 2 || c == 3) {
            m &= ~(COLON_HH_MM_MASK | COLON_MM_SS_MASK);
        }
        LCD->RAM[com_ram[c]] = (LCD->RAM[com_ram[c]] & ~m) | data[c];
        __DSB();
    }
}

/**
 * @brief Show a decimal digit at the given position.
 * @param pos 0..5
 * @param val Digit 0..9
 */
static void lcd_digit(int pos, uint8_t val) {
    lcd_write_pos(pos, NumberMap[val % 10]);
}

/**
 * @brief Clear a digit position (turn off all segments).
 */
static void lcd_clear_pos(int pos) {
    lcd_write_pos(pos, 0x0000);
}

/**
 * @brief Turn the colon dots on or off.
 * @param on 1 = on, 0 = off
 */
static void lcd_colons(int on) {
    if (on) {
        LCD->RAM[4] |= COLON_HH_MM_MASK | COLON_MM_SS_MASK;
    } else {
        LCD->RAM[4] &= ~(COLON_HH_MM_MASK | COLON_MM_SS_MASK);
    }
    __DSB();
    lcd_udr();          // Immediately update the display
}

/**
 * @brief Update the LCD with the current time, optionally blanking one field.
 * @param hh    Hours (0..23)
 * @param mm    Minutes (0..59)
 * @param ss    Seconds (0..59)
 * @param field Field being edited in setup mode:
 *              0 = hours, 1 = minutes, 2 = seconds, -1 = show all normally
 * @param blank If 1, the selected field is blanked (for blinking effect)
 */
static void lcd_show_time(uint8_t hh, uint8_t mm, uint8_t ss, int field, int blank) {
    // Hours
    if (field == 0 && blank) { lcd_clear_pos(0); lcd_clear_pos(1); }
    else { lcd_digit(0, hh / 10); lcd_digit(1, hh % 10); }

    // Minutes
    if (field == 1 && blank) { lcd_clear_pos(2); lcd_clear_pos(3); }
    else { lcd_digit(2, mm / 10); lcd_digit(3, mm % 10); }

    // Seconds
    if (field == 2 && blank) { lcd_clear_pos(4); lcd_clear_pos(5); }
    else { lcd_digit(4, ss / 10); lcd_digit(5, ss % 10); }

    __DSB();
    lcd_udr();          // Update after drawing all digits
}

/* ====================================================================
 * SysTick timer – 1 ms tick
 * ====================================================================
 */
volatile uint32_t ms_tick = 0;

void SysTick_Handler(void) { ms_tick++; }
static uint32_t millis(void) { return ms_tick; }
static void delay_ms(uint32_t ms) {
    uint32_t t = millis();
    while ((millis() - t) < ms);
}

/* ====================================================================
 * Hardware RTC (STM32L1)
 * ====================================================================
 */
static uint8_t bcd2bin(uint8_t bcd) { return (bcd >> 4) * 10 + (bcd & 0x0F); }
static uint8_t bin2bcd(uint8_t bin) { return ((bin / 10) << 4) | (bin % 10); }

static void rtc_unlock(void) { RTC->WPR = 0xCA; RTC->WPR = 0x53; }
static void rtc_lock(void)   { RTC->WPR = 0xFF; }

/**
 * @brief Initialize the RTC with LSE clock, 24‑hour format, and start at 00:00:00.
 */
static void RTC_Init(void) {
    RCC->CSR |= RCC_CSR_RTCEN;          // Enable RTC
    (void)RCC->CSR; __DSB();

    rtc_unlock();
    RTC->ISR |= RTC_ISR_INIT;           // Enter initialization mode
    int tout = 0;
    while (!(RTC->ISR & RTC_ISR_INITF) && tout < 1000000) { tout++; }

    RTC->PRER = (127u << 16) | 255u;    // Prescaler for 1 Hz (LSE = 32768 Hz)
    RTC->CR   &= ~RTC_CR_FMT;           // 24‑hour format
    RTC->TR    = 0x00000000;            // Start at 00:00:00
    RTC->DR    = 0x00002101;            // Date: 1st Jan 2021 (not critical)
    RTC->ISR  &= ~RTC_ISR_INIT;         // Exit init mode
    rtc_lock();
}

/**
 * @brief Read current time from RTC.
 * @param hh, mm, ss Output pointers (binary values)
 */
static void rtc_get_time(uint8_t *hh, uint8_t *mm, uint8_t *ss) {
    RTC->ISR &= ~RTC_ISR_RSF;           // Clear RSF to force sync
    while (!(RTC->ISR & RTC_ISR_RSF));  // Wait for registers to synchronise
    uint32_t tr = RTC->TR;

    // Dummy read of DR to unlock shadow registers
    (void)RTC->DR;

    *hh = bcd2bin((tr >> 16) & 0x3F);
    *mm = bcd2bin((tr >>  8) & 0x7F);
    *ss = bcd2bin((tr >>  0) & 0x7F);
}

/**
 * @brief Set the RTC time.
 * @param hh, mm, ss Binary values (hh 0..23, mm 0..59, ss 0..59)
 */
static void rtc_set_time(uint8_t hh, uint8_t mm, uint8_t ss) {
    rtc_unlock();
    RTC->ISR |= RTC_ISR_INIT;
    int tout = 0;
    while (!(RTC->ISR & RTC_ISR_INITF) && tout < 1000000) { tout++; }

    RTC->TR = ((uint32_t)bin2bcd(hh) << 16)
            | ((uint32_t)bin2bcd(mm) <<  8)
            | ((uint32_t)bin2bcd(ss));

    RTC->ISR &= ~RTC_ISR_INIT;
    rtc_lock();
}

/* ====================================================================
 * User button on PA0
 * ====================================================================
 */
static void button_init(void) {
    GPIOA->MODER &= ~(3u << 0);         // Input mode
    GPIOA->PUPDR &= ~(3u << 0);
    GPIOA->PUPDR |=  (2u << 0);         // Pull‑down
}
static int btn_raw(void) { return (GPIOA->IDR >> 0) & 1; }

/**
 * @brief Wait for the button to be released, with a timeout.
 * @return How many milliseconds the button was held down
 */
static uint32_t btn_wait_release(void) {
    uint32_t t = millis();
    while (btn_raw() && (millis() - t) < 10000);
    return millis() - t;
}

#define LONG_PRESS_MIN_MS 800u

/* ====================================================================
 * Setup mode – edit hours, minutes, seconds
 * ====================================================================
 */
static void setup_mode(void) {
    uint8_t hh, mm, ss;
    // Read current time from RTC
    RTC->ISR &= ~RTC_ISR_RSF;
    while (!(RTC->ISR & RTC_ISR_RSF));
    uint32_t tr = RTC->TR;
    (void)RTC->DR;

    hh = bcd2bin((tr >> 16) & 0x3F);
    mm = bcd2bin((tr >>  8) & 0x7F);
    ss = 0;                             // Seconds start at 0 when editing

    int field = 0;                      // 0=hours, 1=minutes, 2=seconds
    uint32_t blink_t = millis();
    int blink_vis = 1;                  // 1 = visible, 0 = blank

    lcd_colons(1);                      // Show colons during setup

    while (field < 3) {
        // Blink the active field at ~2 Hz (300 ms on, 200 ms off)
        uint32_t phase = (millis() - blink_t) % 500;
        int new_vis = (phase < 300) ? 1 : 0;
        if (new_vis != blink_vis) {
            blink_vis = new_vis;
            lcd_show_time(hh, mm, ss, field, !blink_vis);
            lcd_colons(1);
        }

        if (btn_raw()) {
            delay_ms(30);               // Debounce
            if (!btn_raw()) continue;

            uint32_t dur = btn_wait_release();
            delay_ms(30);

            if (dur >= LONG_PRESS_MIN_MS) {
                // Long press → move to next field
                field++;
                blink_t = millis(); blink_vis = 1;
                lcd_show_time(hh, mm, ss, field < 3 ? field : -1, 0);
                lcd_colons(1);
            } else if (dur >= 20) {
                // Short press → increment current field
                if (field == 0) hh = (hh + 1) % 24;
                else if (field == 1) mm = (mm + 1) % 60;
                else if (field == 2) ss = (ss + 1) % 60;
                blink_vis = 1; blink_t = millis();
                lcd_show_time(hh, mm, ss, field, 0);
                lcd_colons(1);
            }
        }
    }
    rtc_set_time(hh, mm, ss);
}

/* ====================================================================
 * System clock configuration (MSI ~2.097 MHz, LSE for RTC)
 * ====================================================================
 */
void SystemClock_Config(void) {
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR; __DSB();
    // Set voltage scaling to range 1 (max 32 MHz)
    PWR->CR = (PWR->CR & ~PWR_CR_VOS_Msk) | (0x02UL << PWR_CR_VOS_Pos);
    while (PWR->CSR & PWR_CSR_VOSF) {}
    PWR->CR |= PWR_CR_DBP;              // Enable access to RTC and backup registers
    __DSB();

    // Enable LSE (32.768 kHz crystal)
    RCC->CSR |= RCC_CSR_LSEON;
    int tout = 0;
    while (!(RCC->CSR & RCC_CSR_LSERDY) && tout < 50000000) { tout++; }
    RCC->CSR = (RCC->CSR & ~RCC_CSR_RTCSEL_Msk) | RCC_CSR_RTCSEL_LSE;
    __DSB();

    // Enable HSI for system clock (optional, MSI is default)
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) {}
}

/* ====================================================================
 * LCD peripheral initialization
 * ====================================================================
 */
void LCD_Init(void) {
    int tout;
    RCC->APB1ENR |= RCC_APB1ENR_LCDEN;  // Enable LCD clock
    __DSB();
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOBEN | RCC_AHBENR_GPIOCEN;
    __DSB();

    // Configure GPIOs for LCD (as taken from STM32CubeMX)
    GPIOA->MODER = 0xABEAFFAB; GPIOA->OTYPER = 0; GPIOA->OSPEEDR = 0x0C000000;
    GPIOA->PUPDR = 0x64000000; GPIOA->AFR[0] = 0x0000BBB0; GPIOA->AFR[1] = 0xB0000BBB;
    GPIOB->MODER = 0xAAAAFABF; GPIOB->OTYPER = 0; GPIOB->OSPEEDR = 0x000000C0;
    GPIOB->PUPDR = 0x00000100; GPIOB->AFR[0] = 0x00BBB000; GPIOB->AFR[1] = 0xBBBBBBBB;
    GPIOC->MODER = 0xFFAAAAAA; GPIOC->OTYPER = 0; GPIOC->OSPEEDR = 0;
    GPIOC->PUPDR = 0; GPIOC->AFR[0] = 0xBB00BBBB; GPIOC->AFR[1] = 0x0000BBBB;
    __DSB();

    LCD->CR = 0; __DSB();
    // Frame control: prescaler, divider, pulse on duration, dead time, contrast
    LCD->FCR = (0U<<LCD_FCR_PS_Pos) | (1U<<LCD_FCR_DIV_Pos) |
               (3U<<LCD_FCR_PON_Pos) | (3U<<LCD_FCR_DEAD_Pos) | (7U<<LCD_FCR_CC_Pos);
    __DSB();
    tout = 0; while (!(LCD->SR & LCD_SR_FCRSR) && tout < 10000000) { tout++; }

    LCD->CR = 0x000000CC; __DSB();      // Enable LCD, set voltage booster
    LCD->CR |= LCD_CR_LCDEN; __DSB();   // Turn on LCD
    tout = 0; while (!(LCD->SR & LCD_SR_ENS) && tout < 10000000) { tout++; }
    tout = 0; while (!(LCD->SR & LCD_SR_RDY) && tout < 10000000) { tout++; }

    // Clear all RAM
    for (int i = 0; i < 8; i++) LCD->RAM[i] = 0;
    __DSB(); (void)LCD->RAM[7];
    LCD->CLR = LCD_SR_UDD_Msk; __DSB();
    LCD->SR |= LCD_SR_UDR; __DSB();
    tout = 0; while (!(LCD->SR & LCD_SR_UDD_Msk) && tout < 10000000) { tout++; }
}

/* ====================================================================
 * Main program
 * ====================================================================
 */
int main(void) {
    // Initialise mailbox to "empty" before peripherals start
    pc_sync_time = 0xFFFFFFFF;

    SystemClock_Config();
    SysTick_Config(2097000 / 1000);     // 1 ms tick (MSI ~2.097 MHz)

    LCD_Init();
    RTC_Init();
    button_init();

    uint8_t hh, mm, ss;
    uint8_t last_ss = 0xFF;             // Force initial display update

    uint32_t blink_base = 0;
    int current_colon_state = -1;

    while (1) {
        // --- 1. Check for new time from PC ---
        if (pc_sync_time != 0xFFFFFFFF) {
            uint32_t t = pc_sync_time;
            pc_sync_time = 0xFFFFFFFF;  // Mark as read

            // Convert seconds since midnight to hours/minutes/seconds
            uint8_t new_hh = (t / 3600) % 24;
            uint8_t new_mm = (t / 60) % 60;
            uint8_t new_ss = t % 60;

            rtc_set_time(new_hh, new_mm, new_ss);

            // Force display refresh
            last_ss = 0xFF;
            current_colon_state = -1;
        }

        // --- 2. Read current time from RTC ---
        rtc_get_time(&hh, &mm, &ss);

        // --- 3. Update display on each new second ---
        if (ss != last_ss) {
            last_ss = ss;
            blink_base = millis();
            lcd_show_time(hh, mm, ss, -1, 0);   // Show all fields normally
        }

        // --- 4. Blink the colons every 500 ms ---
        int colon_on = ((millis() - blink_base) % 1000) < 500;
        if (colon_on != current_colon_state) {
            current_colon_state = colon_on;
            lcd_colons(colon_on);
        }

        // --- 5. Handle button press (enter setup on long press) ---
        if (btn_raw()) {
            delay_ms(30);
            if (!btn_raw()) continue;

            uint32_t dur = btn_wait_release();
            delay_ms(30);

            if (dur >= LONG_PRESS_MIN_MS) {
                setup_mode();
                last_ss = 0xFF;
                current_colon_state = -1;
            }
        }
    }
}
