#include <EFM8LB1.h>
#include <stdio.h>
#include <string.h>
#include "lcd.h"
#include "global.h"

//analog ADC pins:
#define REF_PEAK_MUX   QFP32_MUX_P2_2
#define TEST_PEAK_MUX  QFP32_MUX_P2_3

//digital GPIO (LM393)
#define REF_ZC   P2_4   // Vref comparator output -> P2.4
#define TEST_ZC  P2_5   // Vtest comparator output -> P2.5

// ---------------------------
// Timer0 overflow extension
// ---------------------------
volatile unsigned long t0_overflows = 0;

// Timer0 overflow ISR (interrupt 1)
void Timer0_ISR(void) interrupt 1
{
    TF0 = 0;          // clear overflow flag
    t0_overflows++;   // count overflows
}

//timer0 init 
void TIMER0_Init(void)
{
    TR0 = 0;
    TMOD &= 0xF0;   // preserve Timer1 (UART baud)
    TMOD |= 0x01;   // Timer0 mode 1 (16-bit timer), C/T=0
    TH0 = 0;
    TL0 = 0;
    TF0 = 0;

    t0_overflows = 0;

    ET0 = 1;        // enable Timer0 interrupt
    EA  = 1;        // global interrupt enable
}


//ADC setup (14-bit, VDD ref)
void InitADC(void)
{
    SFRPAGE = 0x00;
    ADEN = 0;

    ADC0CN1 =
        (0x2 << 6) | // 14-bit
        (0x0 << 3) | // no shift
        (0x0 << 0);  // accumulate 1

    ADC0CF0 =
        ((SYSCLK / SARCLK) << 3) |
        (0x0 << 2);

    ADC0CF1 =
        (0 << 7) |
        (0x1E << 0);

    ADC0CN0 =
        (0x0 << 7) |
        (0x0 << 6) |
        (0x0 << 5) |
        (0x0 << 4) |
        (0x0 << 3) |
        (0x0 << 2) |
        (0x0 << 0);

    ADC0CF2 =
        (0x0 << 7) |
        (0x1 << 5) | // VDD reference
        (0x1F << 0);

    ADC0CN2 =
        (0x0 << 7) |
        (0x0 << 0);

    ADEN = 1;
}

void InitPinADC(unsigned char portno, unsigned char pinno)
{
    unsigned char mask = (1 << pinno);

    SFRPAGE = 0x20;
    switch (portno)
    {
        case 0: P0MDIN &= (~mask); P0SKIP |= mask; break;
        case 1: P1MDIN &= (~mask); P1SKIP |= mask; break;
        case 2: P2MDIN &= (~mask); P2SKIP |= mask; break;
        default: break;
    }
    SFRPAGE = 0x00;
}

static unsigned int ADC_at_Pin(unsigned char pin)
{
    ADC0MX = pin;

    // dummy conversion to settle mux
    ADINT = 0; ADBUSY = 1; while (!ADINT);

    // real conversion
    ADINT = 0; ADBUSY = 1; while (!ADINT);

    return ADC0;
}

static float Volts_at_Pin(unsigned char pin)
{
    // 14-bit ADC max = 16383
    return ((ADC_at_Pin(pin) * VDD) / 16383.0);
}

// Timing helpers
static unsigned long read_T0_ticks32(void)
{
    unsigned long hi1, hi2;
    unsigned int lo;

    // Read overflow counter + TH0/TL0 consistently
    do {
        hi1 = t0_overflows;
        lo  = (((unsigned int)TH0) << 8) | TL0;
        hi2 = t0_overflows;
    } while (hi1 != hi2);

    return (hi1 << 16) | lo;
}

// Full period from REF_ZC: rising-to-rising (robust, duty-cycle independent)
static unsigned long MeasurePeriod_REF(void)
{
    unsigned long t;

    TR0 = 0; TH0 = 0; TL0 = 0; TF0 = 0; t0_overflows = 0;

    // Sync to REF rising edge
    while (REF_ZC == 1);
    while (REF_ZC == 0);

    // Start timing at this rising edge
    TR0 = 0; TH0 = 0; TL0 = 0; TF0 = 0; t0_overflows = 0;
    TR0 = 1;

    // Wait for next REF rising edge (one full period)
    while (REF_ZC == 1);
    while (REF_ZC == 0);

    TR0 = 0;
    t = read_T0_ticks32();
    return t;
}

// Delta ticks: REF rising -> TEST rising (works for lead/lag)
static unsigned long MeasureDeltaTicks_REF_to_TEST(void)
{
    unsigned long dt;

    // Sync to REF rising edge
    while (REF_ZC == 1);
    while (REF_ZC == 0);

    TR0 = 0; TH0 = 0; TL0 = 0; TF0 = 0; t0_overflows = 0;
    TR0 = 1;

    // If TEST is already high (TEST leads), wait for it to go low first
    if (TEST_ZC == 1)
    {
        while (TEST_ZC == 1);
    }

    // Now wait for the next rising edge of TEST
    while (TEST_ZC == 0);

    TR0 = 0;
    dt = read_T0_ticks32();
    return dt;
}

void main(void)
{
    unsigned long period_ticks, delta_ticks;
    long delta_signed;
    float phase_deg, freq_hz;

    float vref_peak, vtest_peak, vref_rms, vtest_rms;

    // --- CHANGE: diode-drop compensation (measured on scope) ---
    const float DIODE_DROP = 0.509f; // volts
    // ----------------------------------------------------------

    char line1[17];
    char line2[17];

    waitms(500);
    printf("\x1b[2J");
    printf("Lab 5: Phase + RMS\n");

    // ADC pins P2.2, P2.3 only
    InitPinADC(2, 2);
    InitPinADC(2, 3);

    InitADC();
    TIMER0_Init();

    LCD_4BIT();
    LCDprint("Lab5 Phase/RMS", 1, 1);
    LCDprint("Loading...", 2, 1);
    waitms(500);

    while (1)
    {
        // 1) Period from REF_ZC (full period, 32-bit, no overflow issues)
        period_ticks = MeasurePeriod_REF();

        if (period_ticks == 0)
        {
            LCDprint("No REF ZC", 1, 1);
            LCDprint("Check P2.4", 2, 1);
            printf("ERROR: period_ticks=0 (no REF_ZC?)\r");
            waitms(250);
            continue;
        }

        // 2) Delta ticks REF->TEST (32-bit)
        delta_ticks = MeasureDeltaTicks_REF_to_TEST();

        // 3) Signed wrap correction (lead/lag)
        delta_signed = (long)delta_ticks;
        if (delta_ticks > (period_ticks / 2UL))
            delta_signed = (long)delta_ticks - (long)period_ticks; // negative if TEST leads

        // 4) Phase degrees
        phase_deg = ((float)delta_signed * 360.0f) / (float)period_ticks;

        // 5) Frequency (Timer0 ticks at SYSCLK/12)
        freq_hz = (float)SYSCLK / (12.0f * (float)period_ticks);

        // 6) ADC peaks -> RMS (with diode-drop compensation)
        vref_peak  = Volts_at_Pin(REF_PEAK_MUX);
        vtest_peak = Volts_at_Pin(TEST_PEAK_MUX);

        vref_rms  = (vref_peak  + DIODE_DROP) / 1.41421356f;
        vtest_rms = (vtest_peak + DIODE_DROP) / 1.41421356f;

        // Serial print
        printf("Vrms_ref=%6.3f V  Vrms_test=%6.3f V  phase=%7.2f deg  f=%6.2f Hz  (T=%lu dt=%ld)\r",
               vref_rms, vtest_rms, phase_deg, freq_hz, period_ticks, delta_signed);

        // LCD (16x2)
        memset(line1, 0, sizeof(line1));
        memset(line2, 0, sizeof(line2));

        sprintf(line1, "R:%1.2f T:%1.2f", vref_rms, vtest_rms);
        sprintf(line2, "Ph:%+5.1f F:%2.0f", phase_deg, freq_hz);

        LCDprint(line1, 1, 1);
        LCDprint(line2, 2, 1);

        waitms(200);
    }
}