/*
 * ThunderSense - adc.c
 * ADC1 single-conversion for internal channels. Vrefint (ch8) gives VDD.
 */
#include "adc.h"
#include "ch32fun.h"

#define ADC_CH_VREFINT   8u
#define VREFINT_MV       1200u     /* internal ref nominal ~1.2 V */

void adc_init(void)
{
    RCC->APB2PCENR |= RCC_APB2Periph_ADC1;
    /* ADC clock = HCLK/8 = 6 MHz (<= 14 MHz max). */
    RCC->CFGR0 = (RCC->CFGR0 & ~RCC_ADCPRE) | RCC_ADCPRE_DIV8_1;

    /* Slow sample time on all channels (internal refs need long sampling). */
    ADC1->SAMPTR1 = 0x00FFFFFF;
    ADC1->SAMPTR2 = 0x3FFFFFFF;

    ADC1->CTLR2 = ADC_ADON;                 /* power on */
    for (volatile int i = 0; i < 4000; i++) { }   /* Tstab */

    ADC1->CTLR2 |= ADC_RSTCAL; while (ADC1->CTLR2 & ADC_RSTCAL) { }
    ADC1->CTLR2 |= ADC_CAL;    while (ADC1->CTLR2 & ADC_CAL)    { }

    ADC1->CTLR2 |= ADC_TSVREFE;             /* enable Vrefint path */
}

uint16_t adc_read(uint8_t ch)
{
    ADC1->RSQR1 = 0;                        /* 1 conversion in the sequence */
    ADC1->RSQR3 = ch & 0x1F;
    ADC1->CTLR2 |= ADC_SWSTART;
    uint32_t to = 0;
    while (!(ADC1->STATR & ADC_EOC) && ++to < 200000u) { }
    return (uint16_t)ADC1->RDATAR;
}

uint16_t adc_vdd_mv(void)
{
    uint16_t v = adc_read(ADC_CH_VREFINT);
    if (!v) return 0;
    /* adc = Vrefint/VDD * 4095  ->  VDD = Vrefint * 4095 / adc */
    return (uint16_t)(((uint32_t)VREFINT_MV * 4095u) / v);
}
