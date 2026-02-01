// CPU over clocked from 16 MHz to 64 MHz
#define F_CPU         64000000UL
#define CPU_PRESCALER 1

#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <string.h>

// Helper macro for repeating draw operation
#define REP0(X)
#define REP1(X) X
#define REP2(X) REP1(X) REP1(X)
#define REP3(X) REP2(X) REP1(X)
#define REP4(X) REP3(X) REP1(X)
#define REP5(X) REP4(X) REP1(X)
#define REP6(X) REP5(X) REP1(X)
#define REP7(X) REP6(X) REP1(X)
#define REP8(X) REP7(X) REP1(X)
#define REP9(X) REP8(X) REP1(X)

#define REP00(X)
#define REP10(X)  REP9(X)  REP1(X)
#define REP20(X) REP10(X) REP10(X)
#define REP30(X) REP20(X) REP10(X)
#define REP40(X) REP30(X) REP10(X)
#define REP50(X) REP40(X) REP10(X)
#define REP60(X) REP50(X) REP10(X)
#define REP70(X) REP60(X) REP10(X)
#define REP80(X) REP70(X) REP10(X)
#define REP90(X) REP80(X) REP10(X)

#define REP(TENS,ONES,X) \
REP##TENS##0(X) \
REP##ONES(X)

// Macros for easier port access
#define SYNC      PORTD
#define VGA       PORTC
#define BLUE0     (1 << 0)
#define BLUE1     (1 << 1)
#define GREEN0    (1 << 2)
#define GREEN1    (1 << 3)
#define RED0      (1 << 6)
#define RED1      (1 << 7)
#define HSYNC     (1 << 0)
#define VSYNC     (1 << 1)
#define SYNCS     (HSYNC | VSYNC)
#define COLORS    (RED0 | RED1 | GREEN0 | GREEN1 | BLUE0 | BLUE1)
#define WHITE     (RED0 | RED1 | GREEN0 | GREEN1 | BLUE0 | BLUE1)
#define BLACK     0

// Extra Mario colors
#define BROWN     0b01001010
#define PINK      0b11000101
#define RED       (RED0 | RED1)

// Macros for enabling and disabling H and V syncs
#define HSYNC_ON    SYNC.OUTCLR = HSYNC
#define HSYNC_OFF   SYNC.OUTSET = HSYNC
#define VSYNC_ON    SYNC.OUTCLR = VSYNC
#define VSYNC_OFF   SYNC.OUTSET = VSYNC

// VGA timing values http://tinyvga.com/vga-timing/640x480@60Hz
#define FRONT_PORCH   (0.63555114200596 * (F_CPU) / 1000000UL - 3)
#define BACK_PORCH    (1.9066534260179 * (F_CPU) / 1000000UL - 60)
#define HSYNC_PULSE   (3.8133068520357 * (F_CPU) / 1000000UL - 3)
#define WHOLE_LINE    (31.777557100298 * (F_CPU) / 1000000UL)
#define VISIBLE_AREA  (25.422045680238 * (F_CPU) / 1000000UL)

// The real number of "tiles"
#define PIXELS_Y    480
#define COLORS_X    60
#define COLORS_Y    60
#define COLORS_T    (COLORS_X * COLORS_Y)
#define MULT_Y      (PIXELS_Y / COLORS_Y)

// Short hand for __builtin_avr_delay_cycles
#define _(CYCLES)   __builtin_avr_delay_cycles(CYCLES);

volatile uint8_t colorMap[COLORS_X * COLORS_Y];

// Let's make these macro values static constants.
// This way we don't need to recalculate them in every place.
static const uint16_t colors = COLORS_T;
static const uint8_t colorsX = COLORS_X;
static const uint8_t multY = MULT_Y;
static const uint16_t front_porch = FRONT_PORCH;
static const uint16_t back_porch = BACK_PORCH;
static const uint16_t hsync_pulse = HSYNC_PULSE;

/*
 * Initializes 16 MHz external crystal by over clocking it to 64 MHz by using PLL
 */
void initClock() {
  // We'll use external 16 MHz crystal and wait for its stabilization
  OSC_XOSCCTRL = OSC_FRQRANGE_12TO16_gc | OSC_XOSCSEL_XTAL_16KCLK_gc;
  OSC_CTRL |= OSC_XOSCEN_bm;
  while (!(OSC_STATUS & OSC_XOSCRDY_bm));
  
  OSC.PLLCTRL = OSC_PLLSRC_XOSC_gc | 4;
  
  // Enable PLL and wait for stabilization
  OSC.CTRL |= OSC_PLLEN_bm;
  while (!(OSC.STATUS & OSC_PLLRDY_bm));
  
  // Safe way to switch to new clock source
  CCP = CCP_IOREG_gc;
  CLK.CTRL = CLK_SCLKSEL_PLL_gc;
}

/*
 * Sets default port directions and values
 */
void initVGA() {
  SYNC.DIRSET = SYNCS;
  VGA.DIRSET = COLORS;
  VGA.OUTCLR = COLORS;
  VSYNC_OFF;
  HSYNC_OFF;
}

/**
 * Initializes Timer Counters 1 and 0
 *
 * TCC1 overflows based on VGA line width.
 * TCC0 counts lines. Incremented every time TCC1 overflow occurs.
 */
void initVGATimer() {
  TCC1.PER = WHOLE_LINE;
  TCC1.CTRLA = (TCC1.CTRLA & ~TC1_CLKSEL_gm) | TC_CLKSEL_DIV1_gc;
  TCC1.INTCTRLA = (TCC1.INTCTRLA & ~TC1_OVFINTLVL_gm) | TC_OVFINTLVL_HI_gc;
  
  EVSYS.CH0MUX = EVSYS_CHMUX_TCC1_OVF_gc;
  
  TCC0.PER = 524;
  TCC0.CTRLA = (TCC0.CTRLA & ~TC0_CLKSEL_gm) | TC_CLKSEL_EVCH0_gc;
  TCC0.INTCTRLA = (TCC0.INTCTRLA & ~TC0_OVFINTLVL_gm) | TC_OVFINTLVL_MED_gc;
  TCC0.CTRLD = (uint8_t) TC_EVSEL_CH0_gc | TC_EVACT_CAPT_gc;
  
  PMIC.CTRL |= PMIC_HILVLEN_bm | PMIC_MEDLVLEN_bm;
}

/**
 * Sets one color in colorMap
 */
void paint(uint16_t x, uint16_t y, uint8_t color) {
  colorMap[x + y * colorsX] = COLORS & color;
}

/**
 * Sets rectangle area of colors in colorMap
 */
void paintArea(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint8_t color) {
  static volatile uint16_t x;
  
  x = x2 - x1 + 1;
  
  while (y1 <= y2) {
    memset((void *) colorMap + x1 + y1++ * colorsX, COLORS & color, x);
  }
}

typedef struct Step Step;

// Step contains information about one or more colors in colorMap.
// Step also has timing information included.
struct Step {
  uint8_t x1;
  uint8_t y1;
  uint8_t x2;
  uint8_t y2;
  uint8_t color;
  uint16_t wait;
};

// Super Mario animation steps
volatile Step steps[] = {
  { 16, 10, 40, 45, BLACK, 8 },
    
  // Frame 1 of Super Mario animation
  
  // HAT
  { 24, 12, 34, 15, RED, 0 },
  { 32, 12, 34, 14, BLACK, 0 },
  { 24, 12, 26, 12, BLACK, 0 },
  { 24, 13, 24, 13, BLACK, 0 },
  { 30, 14, 31, 14, PINK, 0 },
  { 31, 13, 31, 13, PINK, 0 },
  
  // HEAD
  { 22, 16, 34, 22, BROWN, 0 },
  { 22, 31, 22, 31, BLACK, 0 },
  { 22, 15, 23, 16, BLACK, 0 },
  { 22, 16, 22, 18, BLACK, 0 },
  { 33, 16, 34, 16, BLACK, 0 },
  { 34, 22, 34, 22, BLACK, 0 },
  { 22, 21, 22, 21, BLACK, 0 },
  { 22, 22, 23, 22, BLACK, 0 },
  { 24, 22, 24, 22, BLACK, 0 },
  { 32, 18, 35, 19, PINK, 0 },
  { 31, 17, 34, 18, PINK, 0 },
  { 27, 16, 28, 17, PINK, 0 },
  { 30, 16, 32, 16, PINK, 0 },
  { 24, 17, 25, 20, PINK, 0 },
  { 24, 20, 28, 20, PINK, 0 },
  { 25, 21, 29, 21, PINK, 0 },
  { 26, 22, 33, 22, PINK, 0 },
  { 28, 22, 30, 23, PINK, 0 },
  { 28, 18, 30, 19, PINK, 0 },
  
  // BODY
  { 23, 24, 32, 37, RED, 0 },
  { 25, 23, 27, 23, RED, 0 },
  { 33, 34, 33, 35, RED, 0 },
  { 23, 24, 23, 24, BLACK, 0 },
  { 30, 24, 33, 24, BLACK, 0 },
  { 31, 25, 33, 25, BLACK, 0 },
  { 27, 37, 28, 37, BLACK, 0 },
  { 24, 23, 24, 23, BROWN, 0 },
  { 23, 24, 23, 26, BROWN, 0 },
  { 22, 26, 22, 28, BROWN, 0 },
  { 25, 24, 27, 27, BROWN, 0 },
  { 26, 25, 28, 29, BROWN, 0 },
  { 27, 26, 29, 30, BROWN, 0 },
  { 29, 27, 31, 31, BROWN, 0 },
  { 29, 24, 29, 24, BROWN, 0 },
  { 30, 25, 30, 25, BROWN, 0 },
  { 32, 29, 32, 30, BROWN, 0 },
  { 32, 33, 32, 33, BROWN, 0 },
  { 31, 34, 31, 34, BROWN, 0 },
  { 29, 35, 30, 35, BROWN, 0 },
  { 28, 36, 28, 36, BROWN, 0 },
  
  // HAND
  { 32, 26, 33, 28, PINK, 0 },
  { 33, 27, 35, 30, PINK, 0 },
  
  // RIGHT LEG
  { 29, 38, 32, 41, BROWN, 0 },
  { 33, 40, 34, 41, BROWN, 0 },
  
  // LEFT LEG
  { 20, 34, 23, 38, BROWN, 0 },
  { 20, 39, 21, 39, BROWN, 0 },
  { 20, 40, 20, 40, BROWN, 0 },
  
  { 16, 10, 40, 45, BLACK, 8 },
    
  // Frame 2 of Super Mario animation
  
  // HAT
  { 24, 11, 34, 14, RED, 0 },
  { 32, 11, 34, 13, BLACK, 0 },
  { 24, 11, 26, 11, BLACK, 0 },
  { 24, 12, 24, 12, BLACK, 0 },
  { 30, 13, 31, 13, PINK, 0 },
  { 31, 12, 31, 12, PINK, 0 },
  
  // HEAD
  { 22, 15, 34, 21, BROWN, 0 },
  { 22, 30, 22, 30, BLACK, 0 },
  { 22, 14, 23, 15, BLACK, 0 },
  { 22, 15, 22, 17, BLACK, 0 },
  { 33, 15, 34, 15, BLACK, 0 },
  { 34, 21, 34, 21, BLACK, 0 },
  { 22, 20, 22, 20, BLACK, 0 },
  { 22, 21, 23, 21, BLACK, 0 },
  { 32, 17, 35, 18, PINK, 0 },
  { 31, 16, 34, 17, PINK, 0 },
  { 27, 15, 28, 16, PINK, 0 },
  { 30, 15, 32, 15, PINK, 0 },
  { 24, 16, 25, 19, PINK, 0 },
  { 24, 19, 28, 19, PINK, 0 },
  { 25, 20, 29, 20, PINK, 0 },
  { 26, 21, 33, 21, PINK, 0 },
  { 29, 22, 30, 22, PINK, 0 },
  { 28, 17, 30, 18, PINK, 0 },
  
  // BODY
  { 23, 23, 32, 35, BROWN, 0 },
  { 26, 36, 29, 40, BROWN, 0 },
  { 27, 40, 31, 41, BROWN, 0 },
  { 25, 39, 25, 40, BROWN, 0 },
  { 30, 36, 31, 37, BROWN, 0 },
  { 31, 37, 32, 38, BROWN, 0 },
  { 23, 23, 23, 23, BLACK, 0 },
  { 32, 23, 32, 24, BLACK, 0 },
  { 23, 34, 24, 35, BLACK, 0 },
  { 23, 33, 23, 33, BLACK, 0 },
  { 32, 31, 32, 33, RED, 0 },
  { 31, 32, 31, 32, RED, 0 },
  { 33, 29, 33, 33, RED, 0 },
  { 34, 30, 34, 32, RED, 0 },
  { 28, 23, 29, 23, RED, 0 },
  { 29, 24, 30, 26, RED, 0 },
  { 31, 26, 31, 26, RED, 0 },
  { 25, 22, 28, 22, RED, 0 },
  { 25, 23, 25, 23, RED, 0 },
  { 24, 24, 24, 33, RED, 0 },
  { 23, 29, 25, 32, RED, 0 },
  { 24, 32, 28, 33, RED, 0 },
  { 25, 32, 27, 34, RED, 0 },
  { 26, 31, 26, 36, RED, 0 },
  { 27, 37, 29, 37, RED, 0 },
  { 27, 36, 27, 36, RED, 0 },
  { 25, 35, 25, 35, RED, 0 },
  
  // HAND
  { 29, 28, 32, 31, PINK, 0 },
  { 30, 27, 31, 31, PINK, 0 },
  { 32, 31, 32, 31, RED, 0 },
  
  { 16, 10, 40, 45, BLACK, 8 },
    
  // Frame 3 of Super Mario animation
    
  // HAT
  { 25, 10, 35, 13, RED, 0 },
  { 33, 10, 35, 12, BLACK, 0 },
  { 25, 10, 27, 10, BLACK, 0 },
  { 25, 11, 25, 11, BLACK, 0 },
  { 31, 12, 32, 12, PINK, 0 },
  { 32, 11, 32, 11, PINK, 0 },
    
  // HEAD
  { 23, 14, 35, 20, BROWN, 0 },
  { 27, 21, 31, 21, BROWN, 0 },
  { 23, 13, 24, 14, BLACK, 0 },
  { 23, 14, 23, 16, BLACK, 0 },
  { 34, 14, 35, 14, BLACK, 0 },
  { 35, 20, 35, 20, BLACK, 0 },
  { 23, 20, 24, 20, BLACK, 0 },
  { 33, 16, 36, 17, PINK, 0 },
  { 32, 15, 35, 16, PINK, 0 },
  { 28, 14, 29, 15, PINK, 0 },
  { 31, 14, 33, 14, PINK, 0 },
  { 25, 15, 26, 18, PINK, 0 },
  { 25, 18, 29, 18, PINK, 0 },
  { 26, 19, 30, 19, PINK, 0 },
  { 27, 20, 34, 20, PINK, 0 },
  { 30, 21, 31, 21, PINK, 0 },
  { 29, 16, 31, 17, PINK, 0 },
    
  // BODY
  { 25, 22, 34, 36, RED, 0 },
  { 26, 37, 29, 37, RED, 0 },
  { 27, 38, 27, 38, RED, 0 },
  { 25, 22, 25, 22, BLACK, 0 },
  { 33, 22, 34, 22, BLACK, 0 },
  { 34, 23, 34, 23, BLACK, 0 },
  { 34, 24, 34, 24, BROWN, 0 },
  { 33, 25, 33, 28, BROWN, 0 },
  { 32, 23, 32, 27, BROWN, 0 },
  { 24, 24, 29, 26, BROWN, 0 },
  { 25, 23, 28, 23, BROWN, 0 },
  { 23, 25, 28, 27, BROWN, 0 },
  { 22, 28, 27, 28, BROWN, 0 },
  { 22, 29, 25, 29, BROWN, 0 },
  { 30, 22, 31, 22, BROWN, 0 },
  { 31, 23, 32, 23, BROWN, 0 },
  { 26, 34, 26, 34, BROWN, 0 },
  { 27, 35, 27, 35, BROWN, 0 },
  { 28, 36, 29, 36, BROWN, 0 },
  { 31, 28, 31, 28, PINK, 0 },
  { 34, 28, 34, 28, PINK, 0 },
    
  // LEFT HAND
  { 22, 30, 26, 31, PINK, 0 },
  { 22, 32, 25, 32, PINK, 0 },
  { 23, 33, 25, 33, PINK, 0 },
  
  // RIGHT HAND
  { 35, 23, 37, 25, PINK, 0 },
  { 36, 22, 36, 26, PINK, 0 },
  { 35, 26, 35, 28, BROWN, 0 },
  { 36, 27, 36, 27, BROWN, 0 },
  { 37, 26, 37, 26, BROWN, 0 },
    
  // RIGHT LEG
  { 33, 31, 37, 37, BROWN, 0 },
  { 35, 31, 36, 31, BLACK, 0 },
  { 35, 32, 35, 32, BLACK, 0 },
  { 33, 31, 34, 32, RED, 0 },
    
  // LEFT LEG
  { 23, 37, 25, 40, BROWN, 0 },
  { 22, 36, 24, 38, BROWN, 0 },
  { 24, 41, 26, 41, BROWN, 0 },
  { 24, 35, 24, 35, BROWN, 0 },
  { 26, 38, 26, 38, BROWN, 0 },
};

// Current step, wait and count (= total number of steps)
volatile uint16_t wait = 0;
volatile uint8_t step = 0;
volatile uint8_t count = sizeof(steps) / sizeof(*steps);

int main(void) {
  cli();
  
  // Let's fill colorMap with a black color
  memset((void *) colorMap, BLACK, colors);
  
  initClock();
  initVGA();
  initVGATimer();
    
  sei();

  while (1) { }
}

ISR(TCC0_OVF_vect) {
  // We'll move our Super Mario by using this offset
  static volatile uint8_t offset = 0;
  
  if (offset == 60) {
    offset = 0;
  }
  if (step < count) {
    if (wait == 0) {
      do {
        paintArea(
          steps[step].x1 + offset,
          steps[step].y1,
          steps[step].x2 + offset,
          steps[step].y2,
          steps[step].color
        );
        step++;
      } while (steps[step].wait == 0 && step < count);
      
      offset += 4;
      
      wait = steps[step].wait;
    } else {
      wait--;
    }
  } else {
    step = 0;
    wait = steps[step].wait;
  }
}

/*
 * This interrupt controls VGA generation
 */
ISR(TCC1_OVF_vect) {
  static volatile uint16_t line = 0;
  static volatile uint16_t tile = 0;
  
  // Front porch = 0,63555114200596 us
  _(front_porch);

  // HSync = 3,8133068520357 us
  HSYNC_ON; // 3
    
  _(hsync_pulse);
  
  HSYNC_OFF; // 3

  // Back porch = 1,9066534260179 us
  _(back_porch);

  if (490 == line) { // 9
    VSYNC_ON;
  }
  if (492 == line) { // 10
    VSYNC_OFF;
  }
  if (524 == line) { // 10
    line = 0;
    tile = ((line + 1) / multY) * colorsX;
  } else {
    if (line < 480) { // 9
      // Output colors to monitor
      REP(6, 0, VGA.OUT = colorMap[tile++]; _(2););
      _(18);
      VGA.OUTCLR = COLORS; // 3
    }
    line++; // 12
    tile = (line / multY) * colorsX;
  }
}
