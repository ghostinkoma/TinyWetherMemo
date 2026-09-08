/*
 * ThunderSense - i2c_slave.c
 * HW I2C1 slave on the upper bus + DMA-TX for bundle/diag responses.
 * (Docs/SPEC.md §5, §8; Docs/I2C_REFERENCE.md)
 *
 * Protocol pattern (I2C_REFERENCE §2):
 *   Read : S ADDR+W [opcode] Sr ADDR+R [data...] P   (opcode arrives in the
 *          short write phase; the following repeated-START read is served by DMA)
 *   Write: S ADDR+W [opcode][args...] P              (buffered, executed later)
 *
 * Control/data split: the ISR only *receives* command bytes into a buffer and
 * *arms* DMA for reads. Side-effecting commands run later in the main loop
 * (i2c_slave_process_cmd) so lightning capture keeps priority.
 *
 * NOTE: I2C-slave + DMA on CH32V003 needs bench bring-up. Register bit values
 * follow the CH32V003 RM; verify DMA channel (I2C1_TX assumed = DMA1_CH6).
 */
#include "i2c_slave.h"
#include "config.h"
#include "bundle.h"
#include "softclock.h"
#include "capture.h"
#include "as3935.h"
#include "nvcal.h"
#include "debug.h"
#include "ch32fun.h"

/* I2C_* (CTLR1/CTLR2/STAR1/STAR2) and DMA_CFGR1_* bit macros are provided by
 * ch32fun (ch32v003hw.h) - do not redefine them here. */

/* ---- command / response state ---- */
static CmdBuf   g_cmd;                 /* pending write-command for main loop */
static uint8_t  g_rxbuf[8];
static volatile uint8_t g_rxn = 0;
static ts_state_t g_state = STATE_BOOT;

/* diag counters (read from main loop) */
volatile uint32_t g_i2c_addr_hits = 0;   /* ISR: 自分宛アドレス一致(=HWがACKした)回数 */
volatile uint32_t g_i2c_resets    = 0;   /* i2c_slave_reset() を呼んだ回数            */

static DiagStatus g_diag;              /* response buffers (DMA sources)      */
static uint8_t    g_time[6];
static uint8_t    g_info[8];
static HealthData g_health;            /* updated from the main loop (no ADC in ISR) */
static uint8_t    g_regread[2];        /* [reg,val] from CMD_REG_SELECT */
static CalibInfo  g_calib;

void i2c_slave_set_health(uint16_t vdd_mv, uint16_t vrefint_raw)
{
    g_health.vdd_mv      = vdd_mv;
    g_health.vrefint_raw = vrefint_raw;
    g_health.temp_c10    = 0;          /* no calibrated die-temp on CH32V003 */
    g_health.flags       = 0x01;       /* temp is a proxy only */
    g_health.reserved    = 0;
}

/* ---------------- helpers ---------------- */
static uint8_t build_status(void)
{
    uint8_t st = (uint8_t)(g_state & ST_STATE_MASK);
    if (capture_in_progress())      st |= ST_BUSY;
    if (softclock_time_valid())     st |= ST_TIME_VALID;
    if (g_state == STATE_READY && !capture_in_progress()) st |= ST_DATA_OK;
    return st;   /* ST_OVERFLOW is added inside bundle_freeze_for_read */
}

static void dma_tx(const void *src, uint16_t len)
{
    DMA1_Channel6->CFGR  = 0;                       /* disable + reset */
    DMA1_Channel6->PADDR = (uint32_t)&I2C1->DATAR;
    DMA1_Channel6->MADDR = (uint32_t)src;
    DMA1_Channel6->CNTR  = len;
    DMA1_Channel6->CFGR  = DMA_CFGR1_DIR | DMA_CFGR1_MINC | DMA_CFGR1_EN; /* 8-bit */
    I2C1->CTLR2 |= I2C_CTLR2_DMAEN;
}

static void dma_stop(void)
{
    I2C1->CTLR2 &= ~I2C_CTLR2_DMAEN;
    DMA1_Channel6->CFGR = 0;
}

/* Fill a response buffer and arm DMA based on the last received opcode. */
static void serve_read(uint8_t opcode)
{
    switch (opcode) {
    case CMD_READ_BUNDLE: {
        uint16_t len;
        Bundle *b = bundle_freeze_for_read(build_status(), &len);
        dma_tx(b, len);
        break;
    }
    case CMD_READ_STATUS: {
        g_diag.status = build_status();
        g_diag.active_gen = 0;                 /* (filled by bundle if needed) */
        g_diag.ship_len   = bundle_pending_count();
        g_diag.boot_id    = 0;                 /* set at init if desired       */
        uint16_t lt, dt, nt, lo;               /* avoid &packed-member         */
        bundle_get_totals(&lt, &dt, &nt, &lo);
        g_diag.lightning_total = lt; g_diag.disturber_total = dt;
        g_diag.noise_total     = nt; g_diag.lost_total      = lo;
        dma_tx(&g_diag, sizeof(g_diag));
        break;
    }
    case CMD_READ_TIME: {
        uint32_t es; uint16_t ms;
        softclock_now(&es, &ms);
        g_time[0]=(uint8_t)es; g_time[1]=(uint8_t)(es>>8);
        g_time[2]=(uint8_t)(es>>16); g_time[3]=(uint8_t)(es>>24);
        g_time[4]=(uint8_t)ms; g_time[5]=(uint8_t)(ms>>8);
        dma_tx(g_time, sizeof(g_time));
        break;
    }
    case CMD_READ_HEALTH:
        dma_tx(&g_health, sizeof(g_health));
        break;
    case CMD_READ_REG:
        dma_tx(g_regread, sizeof(g_regread));
        break;
    case CMD_READ_CALIB:
        g_calib.tuncap     = as3935_last_tuncap();
        g_calib.ok         = as3935_cal_ok();
        g_calib.count      = as3935_cal_count();
        g_calib.target     = as3935_cal_target();
        g_calib.rco_status = 0;              /* read 0x3A/0x3B via REG passthrough */
        g_calib.div        = AS3935_DIV;
        dma_tx(&g_calib, sizeof(g_calib));
        break;
    case CMD_READ_INFO:
    default:
        g_info[0]='T'; g_info[1]='S';
        g_info[2]=1; g_info[3]=0;               /* fw major.minor */
        g_info[4]=(uint8_t)sizeof(EventBin);    /* record size = 12 */
        g_info[5]=N_BINS;                        /* fifo depth = 32 */
        g_info[6]=0; g_info[7]=0;
        dma_tx(g_info, sizeof(g_info));
        break;
    }
}

/* ---------------- init ---------------- */
void i2c_slave_init(uint8_t addr)
{
    /* Clocks: GPIOC + AFIO (APB2), I2C1 (APB1), DMA1 (AHB). */
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO;
    RCC->APB1PCENR |= RCC_APB1Periph_I2C1;
    RCC->AHBPCENR  |= RCC_AHBPeriph_DMA1;

    /* PC1/PC2 as AF open-drain, 10 MHz (MODE=01, CNF=11 -> 0xD per nibble). */
    GPIOC->CFGLR &= ~((0xFu << (HWI2C_SDA_PIN*4)) | (0xFu << (HWI2C_SCL_PIN*4)));
    GPIOC->CFGLR |=  ((0xDu << (HWI2C_SDA_PIN*4)) | (0xDu << (HWI2C_SCL_PIN*4)));

    /* Reset then configure I2C1. */
    I2C1->CTLR1 |=  I2C_CTLR1_SWRST;
    I2C1->CTLR1 &= ~I2C_CTLR1_SWRST;

    I2C1->CTLR2 = (SYS_CLK_HZ/1000000u) & 0x3F;   /* FREQ = PCLK1 MHz (=SYS_CLK_HZ) */
    I2C1->OADDR1 = (uint16_t)((addr << 1)) | (1u<<14); /* 7-bit addr, bit14=1 */
    I2C1->CTLR1 |= I2C_CTLR1_ACK | I2C_CTLR1_PE;  /* ACK enable + peripheral on */

    /* Interrupts: events + errors (buffer int lets us catch RXNE/TXE). */
    I2C1->CTLR2 |= I2C_CTLR2_ITEVTEN | I2C_CTLR2_ITBUFEN | I2C_CTLR2_ITERREN;

    NVIC_SetPriority(I2C1_EV_IRQn, 0x80);   /* below EXTI(0)/SysTick */
    NVIC_SetPriority(I2C1_ER_IRQn, 0x80);
    NVIC_EnableIRQ(I2C1_EV_IRQn);
    NVIC_EnableIRQ(I2C1_ER_IRQn);

    g_cmd.pending = 0;
    g_rxn = 0;
}

/* ---------------- event ISR ---------------- */
void i2c_slave_isr(void)
{
    uint16_t s1 = I2C1->STAR1;

    if (s1 & I2C_STAR1_ADDR) {
        g_i2c_addr_hits++;                  /* diag: 上位バスで自分宛に呼ばれた */
        uint16_t s2 = I2C1->STAR2;          /* reading STAR1+STAR2 clears ADDR */
        if (s2 & I2C_STAR2_TRA) {
            /* Master is reading us -> serve response by DMA. */
            uint8_t op = (g_rxn > 0) ? g_rxbuf[0] : CMD_READ_BUNDLE;
            serve_read(op);
        } else {
            /* Master is writing to us -> start collecting command bytes. */
            g_rxn = 0;
        }
        return;
    }

    if (s1 & I2C_STAR1_RXNE) {
        uint8_t d = (uint8_t)I2C1->DATAR;
        if (g_rxn < sizeof(g_rxbuf)) g_rxbuf[g_rxn++] = d;
        return;
    }

    if (s1 & I2C_STAR1_STOPF) {
        /* Clear STOPF: read STAR1 (done) then write CTLR1. */
        I2C1->CTLR1 |= I2C_CTLR1_PE;
        /* A completed write transaction = a command for the main loop. */
        if (g_rxn >= 1 && !g_cmd.pending) {
            g_cmd.opcode = g_rxbuf[0];
            for (uint8_t i = 0; i < 6 && (i+1) < g_rxn; i++)
                g_cmd.arg[i] = g_rxbuf[i+1];
            g_cmd.pending = 1;
        }
        g_rxn = 0;
        return;
    }
}

/* ch32fun vector -> our handler */
void I2C1_EV_IRQHandler(void) __attribute__((interrupt));
void I2C1_EV_IRQHandler(void) { i2c_slave_isr(); }

/* Error ISR: on NACK at end of a slave transmit (master read done), and on
 * bus errors, stop DMA and clear flags. */
void I2C1_ER_IRQHandler(void) __attribute__((interrupt));
void I2C1_ER_IRQHandler(void)
{
    uint16_t s1 = I2C1->STAR1;
    if (s1 & I2C_STAR1_AF) {
        I2C1->STAR1 &= ~I2C_STAR1_AF;   /* clear ACK-failure (end of read)  */
        dma_stop();
    }
    /* Clear any other error bits defensively. */
    I2C1->STAR1 &= ~(I2C_STAR1_BERR | I2C_STAR1_ARLO | I2C_STAR1_OVR);
}

/* ---------------- main-loop command execution ---------------- */
void i2c_slave_process_cmd(void)
{
    if (!g_cmd.pending) return;

    switch (g_cmd.opcode) {
    case CMD_CLEAR: {
        uint8_t  gen = g_cmd.arg[0];
        uint16_t crc = (uint16_t)(g_cmd.arg[1] | (g_cmd.arg[2] << 8));
        bundle_clear(gen, crc);
        break;
    }
    case CMD_SET_TIME: {
        uint32_t es = (uint32_t)g_cmd.arg[0] | ((uint32_t)g_cmd.arg[1]<<8)
                    | ((uint32_t)g_cmd.arg[2]<<16) | ((uint32_t)g_cmd.arg[3]<<24);
        uint16_t ms = (uint16_t)(g_cmd.arg[4] | (g_cmd.arg[5]<<8));
        softclock_set_epoch(es, ms);
        DBG("[cmd] SET_TIME %lu.%03u\n", (unsigned long)es, ms);
        break;
    }
    case CMD_CONFIG:
        as3935_config(g_cmd.arg[0], g_cmd.arg[1], g_cmd.arg[2],
                      g_cmd.arg[3], g_cmd.arg[4]);
        break;
    case CMD_SENS:                         /* reg0x02: srej, min_num_ligh */
        as3935_set_sensitivity(g_cmd.arg[0], g_cmd.arg[1]);
        break;
    case CMD_REG_WRITE:                    /* write ANY register */
        as3935_write_reg(g_cmd.arg[0], g_cmd.arg[1]);
        break;
    case CMD_REG_SELECT: {                 /* read reg -> cache for CMD_READ_REG */
        uint8_t v = 0xFF;
        as3935_read_reg(g_cmd.arg[0], &v);
        g_regread[0] = g_cmd.arg[0];
        g_regread[1] = v;
        break;
    }
    case CMD_CMD:
        if (g_cmd.arg[0] == 0x01) {           /* recalibrate + persist to flash */
            if (as3935_calibrate() == AS_OK)
                nvcal_save(as3935_last_tuncap());
        } else if (g_cmd.arg[0] == 0xA5) {
            i2c_slave_reset();
        }
        /* 0x02 flush handled by clearing on next reads */
        break;
    default:
        break;
    }
    g_cmd.pending = 0;
}

void i2c_slave_set_state(ts_state_t state) { g_state = state; }

uint8_t i2c_slave_stuck(void)
{
    /* BUG FIX: 以前は BUSY を見た瞬間に stuck 判定していたが、BUSY は「他マスタ
     * (ESP)が通常通信中」でも立つ。上位バスは常に賑やかなので、これだと毎ループ
     * i2c_slave_reset() が走りスレーブが再init され続け、ホストの ADDR を取り
     * こぼす(=0x28 NG)。本当の wedge は「BUSY が連続して長時間続く」場合だけ。
     * → BUSY が連続 25ms 以上のときのみ stuck とみなす。 */
    static uint64_t busy_since = 0;
    if (I2C1->STAR2 & I2C_STAR2_BUSY) {
        uint64_t now = softclock_ms();
        if (busy_since == 0) busy_since = now ? now : 1;
        return ((uint32_t)(now - busy_since) > 25u) ? 1 : 0;
    }
    busy_since = 0;
    return 0;
}

void i2c_slave_reset(void)
{
    g_i2c_resets++;
    dma_stop();
    I2C1->CTLR1 |= I2C_CTLR1_SWRST;
    for (volatile int i = 0; i < 100; i++) { }
    I2C1->CTLR1 &= ~I2C_CTLR1_SWRST;
    i2c_slave_init(HWI2C_SLAVE_ADDR);
}
