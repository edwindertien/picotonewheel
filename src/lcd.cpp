// ============================================================
//  lcd.cpp  —  LCD driver  (Pico 2 optimised)
//  ST7789V  240×135  RGB565  SPI1
//
//  Pico 2 has 520 KB SRAM — enough for a full 64 KB framebuffer.
//  No tile/dirty-flag system needed. We maintain a full 240×135
//  framebuffer in RAM. Regions are redrawn into it as needed, then
//  flushed to the display in one DMA-friendly SPI burst.
//
//  Dirty flag is per-region (top/bars/bottom) so we only re-render
//  changed regions, but always push the full buffer to the LCD.
//  This simplifies rendering dramatically.
// ============================================================

#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#include "lcd.h"
#include "tonewheel_manager.h"

// ---- Pins --------------------------------------------------
#define LCD_SCK   10
#define LCD_MOSI  11
#define LCD_CS     9
#define LCD_DC     8
#define LCD_RST   12
#define LCD_BL    13

#define BTN_UP     2
#define BTN_DOWN  18
#define BTN_LEFT  16
#define BTN_RIGHT 20
#define BTN_PRESS  3
#define BTN_A     15
#define BTN_B     17

// ---- Screen ------------------------------------------------
#define LCD_W  240
#define LCD_H  135

// ---- Full framebuffer — 63 KB, fine on Pico 2 520 KB SRAM --
static uint16_t FB[LCD_W * LCD_H];

// ---- Layout regions ----------------------------------------
#define TOP_Y      0
#define TOP_H     18
#define BAR_Y     19
#define BAR_H     90
#define STAT_Y   110
#define STAT_H    25

// Drawbar geometry
#define N_BARS    9
#define BAR_W    22
#define BAR_GAP   3
#define BAR_X0    9
#define BAR_FILL_TOP  (BAR_Y + 4)   // top of fill area (4px padding)
#define BAR_FILL_H    76            // fill height in pixels
#define BAR_FILL_BOT  (BAR_FILL_TOP + BAR_FILL_H)
#define LABEL_Y  (BAR_Y + BAR_H - 9)

// ---- Colours -----------------------------------------------
#define C_BLACK    0x0000
#define C_WHITE    0xFFFF
#define C_TOPBG    0x2104
#define C_STATBG   0x2104
#define C_DARKBG   0x1082
#define C_GRAY     0x4A49
#define C_LGRAY    0x8410
#define C_AMBER    0xFD00
#define C_AMBERLO  0x3880
#define C_GREEN    0x07E0
#define C_GREENDIM 0x0320
#define C_ORANGE   0xFB00
#define C_RED      0xD8C0
#define C_BLUE     0x055F
#define C_CYAN     0x07FF

// ---- Framebuffer pixel/rect helpers ------------------------
static inline void fb_px(int x, int y, uint16_t c) {
    if (x >= 0 && x < LCD_W && y >= 0 && y < LCD_H)
        FB[y * LCD_W + x] = c;
}
static void fb_rect(int x, int y, int w, int h, uint16_t c) {
    for (int row = y; row < y+h && row < LCD_H; row++)
        for (int col = x; col < x+w && col < LCD_W; col++)
            FB[row * LCD_W + col] = c;
}

// ---- 5×7 font ----------------------------------------------
static const uint8_t FONT[][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},
    {0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},
    {0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},
    {0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x40,0x3C},{0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7F,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00},{0x10,0x08,0x08,0x10,0x08},
};

static void fb_char(int x, int y, char c, uint16_t fg, uint16_t bg, int sc=1) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t* g = FONT[c-32];
    for (int col=0; col<5; col++)
        for (int row=0; row<7; row++) {
            uint16_t px = (g[col]>>row)&1 ? fg : bg;
            for (int sy=0; sy<sc; sy++)
                for (int sx=0; sx<sc; sx++)
                    fb_px(x+col*sc+sx, y+row*sc+sy, px);
        }
}
static void fb_str(int x, int y, const char* s, uint16_t fg, uint16_t bg, int sc=1) {
    for (; *s; s++, x += sc*6) fb_char(x, y, *s, fg, bg, sc);
}
static void fb_hbar(int x, int y, int w, int h, int val, int maxv, uint16_t fg, uint16_t bg) {
    int f = maxv ? val*w/maxv : 0;
    if (f > 0) fb_rect(x, y, f, h, fg);
    if (f < w) fb_rect(x+f, y, w-f, h, bg);
}
static void fb_dotbar(int x, int y, int val, int maxv, int sz, uint16_t fg, uint16_t bg) {
    for (int i=0; i<maxv; i++)
        fb_rect(x+i*(sz+1), y, sz, sz, i<val ? fg : bg);
}

// ---- SPI / ST7789 ------------------------------------------
static SPISettings _spi(62500000, MSBFIRST, SPI_MODE0);

static void _cmd(uint8_t c) {
    digitalWrite(LCD_DC,LOW);  digitalWrite(LCD_CS,LOW);
    SPI1.transfer(c);
    digitalWrite(LCD_CS,HIGH);
}
static void _dat(uint8_t d) {
    digitalWrite(LCD_DC,HIGH); digitalWrite(LCD_CS,LOW);
    SPI1.transfer(d);
    digitalWrite(LCD_CS,HIGH);
}
static void _win(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1) {
    // ST7789 RAM offset for 240×135 display: col+40, row+53
    x0+=40; x1+=40; y0+=53; y1+=53;
    _cmd(0x2A);
    digitalWrite(LCD_DC,HIGH); digitalWrite(LCD_CS,LOW);
    SPI1.transfer(x0>>8); SPI1.transfer(x0);
    SPI1.transfer(x1>>8); SPI1.transfer(x1);
    digitalWrite(LCD_CS,HIGH);
    _cmd(0x2B);
    digitalWrite(LCD_DC,HIGH); digitalWrite(LCD_CS,LOW);
    SPI1.transfer(y0>>8); SPI1.transfer(y0);
    SPI1.transfer(y1>>8); SPI1.transfer(y1);
    digitalWrite(LCD_CS,HIGH);
    _cmd(0x2C);
}

// Push a horizontal band of the framebuffer to the LCD
static void _flush_rows(int y0, int y1) {
    _win(0, y0, LCD_W-1, y1);
    uint32_t n = (uint32_t)(y1-y0+1) * LCD_W;
    digitalWrite(LCD_DC,HIGH); digitalWrite(LCD_CS,LOW);
    for (uint32_t i=0; i < n; i++) {
        uint16_t px = FB[(y0 * LCD_W) + i];
        SPI1.transfer(px >> 8);
        SPI1.transfer(px & 0xFF);
    }
    digitalWrite(LCD_CS,HIGH);
}

// ---- State -------------------------------------------------
static bool   _d_top  = true;
static bool   _d_bars = true;
static bool   _d_stat = true;
static int    _sel = 2;
static bool   _bp[7]={}, _bc[7]={};
static const int BPINS[7]={BTN_UP,BTN_DOWN,BTN_LEFT,BTN_RIGHT,BTN_PRESS,BTN_A,BTN_B};
static uint32_t _midi_until = 0;
static const char* FOOTAGE[9] = {"16","5.3","8 ","4  ","2.7","2  ","1.6","1.3","1  "};

// ---- Region renderers --------------------------------------

static void _draw_top(const TonewheelManager& org) {
    fb_rect(0, TOP_Y, LCD_W, TOP_H, C_TOPBG);

    // Preset name
    const char* name = "Custom";
    const uint8_t* l = org.drawbars.level;
    if (l[0]==8&&l[1]==8&&l[2]==8&&l[3]==0&&l[4]==0&&l[5]==0&&l[6]==0&&l[7]==0&&l[8]==0)
        name = "Full organ";
    else if (l[0]==8&&l[1]==6&&l[2]==8&&l[3]==8&&l[4]==0&&l[5]==0&&l[6]==0&&l[7]==0&&l[8]==0)
        name = "Jazz";
    else if (l[0]==0&&l[1]==0&&l[2]==4&&l[3]==0&&l[4]==2&&l[5]==0&&l[6]==0&&l[7]==0&&l[8]==0)
        name = "Flute";
    fb_str(6, TOP_Y+5, name, C_WHITE, C_TOPBG);

    // Selected drawbar value
    char buf[14];
    snprintf(buf, sizeof(buf), "DB%d  %d/8", _sel+1, l[_sel]);
    fb_str(90, TOP_Y+5, buf, C_AMBER, C_TOPBG);

    // Voice count — red when at cap
    uint8_t ac = org.activeCount();
    snprintf(buf, sizeof(buf), "%d/%d", ac, MAX_ACTIVE_VOICES);
    uint16_t vc = (ac >= MAX_ACTIVE_VOICES) ? C_RED : C_GREEN;
    fb_str(185, TOP_Y+5, buf, vc, C_TOPBG);

    // MIDI dot
    uint16_t mc = (millis() < _midi_until) ? C_GREEN : C_GREENDIM;
    fb_rect(228, TOP_Y+5, 8, 8, mc);
    // Rounded corners
    fb_px(228, TOP_Y+5, C_TOPBG); fb_px(235, TOP_Y+5, C_TOPBG);
    fb_px(228, TOP_Y+12,C_TOPBG); fb_px(235, TOP_Y+12,C_TOPBG);
}

static void _draw_bars(const TonewheelManager& org) {
    fb_rect(0, BAR_Y, LCD_W, BAR_H, C_BLACK);

    for (int bar=0; bar<N_BARS; bar++) {
        int bx    = BAR_X0 + bar*(BAR_W+BAR_GAP);
        int lev   = org.drawbars.level[bar];
        int fillh = lev * BAR_FILL_H / 8;   // height of pulled-out (amber) region
        bool sel  = (bar == _sel);

        // Pulled-out part: amber at the TOP (bar pulled down from top)
        if (fillh > 0)
            fb_rect(bx, BAR_FILL_TOP, BAR_W, fillh, sel ? C_WHITE : C_AMBER);
        // Pushed-in part: dim below
        if (fillh < BAR_FILL_H)
            fb_rect(bx, BAR_FILL_TOP+fillh, BAR_W, BAR_FILL_H-fillh, sel ? 0x2104 : C_AMBERLO);
        // Separator line at the boundary
        if (lev > 0 && lev < 8)
            fb_rect(bx, BAR_FILL_TOP+fillh, BAR_W, 1, sel ? C_WHITE : C_AMBER);

        // Level digit above bar
        char d[2] = {(char)('0'+lev), 0};
        uint16_t dc = sel ? C_WHITE : (lev>0 ? C_AMBER : C_GRAY);
        fb_char(bx + BAR_W/2 - 2, BAR_FILL_TOP-9, d[0], dc, C_BLACK);

        // Footage label below bar
        uint16_t lc = sel ? C_WHITE : C_GRAY;
        fb_str(bx, LABEL_Y, FOOTAGE[bar], lc, C_BLACK);
    }
}

static void _draw_status(const TonewheelManager& org) {
    fb_rect(0, STAT_Y, LCD_W, STAT_H, C_STATBG);
    fb_rect(0, STAT_Y, LCD_W, 1, C_GRAY);  // top divider line

    int ty = STAT_Y + 5;   // text baseline
    int iy = STAT_Y + 4;   // icon baseline

    // MIDI dot
    fb_str(4, ty, "M", C_CYAN, C_STATBG);
    uint16_t mc = (millis() < _midi_until) ? C_GREEN : C_GREENDIM;
    fb_rect(14, iy+1, 5, 5, mc);

    // Percussion
    uint16_t pc = org.perc.enabled ? C_ORANGE : C_GRAY;
    fb_str(24, ty, "P", pc, C_STATBG);
    fb_rect(33, iy, 10, 10, org.perc.enabled ? C_ORANGE : C_DARKBG);
    if (!org.perc.enabled) fb_rect(34, iy+1, 8, 8, C_STATBG);
    char hc[2] = {(char)('0'+(org.perc.thirdHarmonic?3:2)), 0};
    fb_str(46, ty, hc, pc, C_STATBG);
    fb_rect(54, iy+2, 6, 6, org.perc.fast ? C_ORANGE : C_GRAY);
    fb_rect(63, iy+3, 4, 4, org.perc.soft ? C_GRAY : C_ORANGE);

    // Click dot bar
    fb_str(84, ty, "C", C_RED, C_STATBG);
    fb_dotbar(94, iy+1, org.click.level*9/127, 9, 4, C_RED, C_DARKBG);

    // Volume bar — driven by CC7 (masterVolume 0–255)
    fb_str(144, ty, "V", C_BLUE, C_STATBG);
    fb_hbar(154, iy+2, 90, 6, org.masterVolume, 255, C_BLUE, C_DARKBG);
}

// ---- Public API --------------------------------------------

void ui_mark_dirty_top()    { _d_top  = true; }
void ui_mark_dirty_bars()   { _d_bars = true; }
void ui_mark_dirty_bottom() { _d_stat = true; }
void ui_mark_all_dirty()    { _d_top=true; _d_bars=true; _d_stat=true; }
void ui_midi_activity()     { _midi_until=millis()+120; _d_top=true; _d_stat=true; }

void ui_flush(const TonewheelManager& org) {
    SPI1.beginTransaction(_spi);
    if (_d_top)  { _draw_top(org);    _flush_rows(TOP_Y,  TOP_Y+TOP_H-1);   _d_top=false;  }
    if (_d_bars) { _draw_bars(org);   _flush_rows(BAR_Y,  BAR_Y+BAR_H-1);   _d_bars=false; }
    if (_d_stat) { _draw_status(org); _flush_rows(STAT_Y, STAT_Y+STAT_H-1); _d_stat=false; }
    SPI1.endTransaction();
}

void ui_handle_buttons(TonewheelManager& org) {
    for (int i=0;i<7;i++) { _bp[i]=_bc[i]; _bc[i]=!digitalRead(BPINS[i]); }
    auto edge=[](int i){ return _bc[i]&&!_bp[i]; };

    if (edge(2)) { if(_sel>0){_sel--; ui_mark_dirty_bars(); ui_mark_dirty_top();} }
    if (edge(3)) { if(_sel<8){_sel++; ui_mark_dirty_bars(); ui_mark_dirty_top();} }
    if (edge(1)) {  // DOWN → pull bar down → increase level
        uint8_t& lv=org.drawbars.level[_sel];
        if(lv<8){lv++;org.propagateDrawbars();ui_mark_dirty_bars();ui_mark_dirty_top();}
    }
    if (edge(0)) {  // UP → push bar up → decrease level
        uint8_t& lv=org.drawbars.level[_sel];
        if(lv>0){lv--;org.propagateDrawbars();ui_mark_dirty_bars();ui_mark_dirty_top();}
    }
    if (edge(4)) {
        static int pr=0; pr=(pr+1)%3;
        if(pr==0) org.drawbars.presetFullOrgan();
        else if(pr==1) org.drawbars.presetJazz();
        else org.drawbars.presetFlute();
        org.propagateDrawbars(); ui_mark_all_dirty();
    }
    // Button A: cycle through percussion presets
    // off → 2nd/fast/norm → 2nd/slow/norm → 2nd/fast/soft
    //      → 3rd/fast/norm → 3rd/slow/norm → 3rd/fast/soft → off
    if (edge(5)) {
        static int percState = 0;
        percState = (percState + 1) % 7;
        switch (percState) {
            case 0: org.perc.enabled=false; break;
            case 1: org.perc.enabled=true; org.perc.thirdHarmonic=false; org.perc.fast=true;  org.perc.soft=false; break;
            case 2: org.perc.enabled=true; org.perc.thirdHarmonic=false; org.perc.fast=false; org.perc.soft=false; break;
            case 3: org.perc.enabled=true; org.perc.thirdHarmonic=false; org.perc.fast=true;  org.perc.soft=true;  break;
            case 4: org.perc.enabled=true; org.perc.thirdHarmonic=true;  org.perc.fast=true;  org.perc.soft=false; break;
            case 5: org.perc.enabled=true; org.perc.thirdHarmonic=true;  org.perc.fast=false; org.perc.soft=false; break;
            case 6: org.perc.enabled=true; org.perc.thirdHarmonic=true;  org.perc.fast=true;  org.perc.soft=true;  break;
        }
        org.propagateDrawbars();
        ui_mark_dirty_bottom(); ui_mark_dirty_top();
    }

    // Button B: toggle click between off and max
    if (edge(6)) {
        org.click.level = (org.click.level == 0) ? 127 : 0;
        ui_mark_dirty_bottom();
    }
}

void lcd_begin() {
    pinMode(LCD_CS,OUTPUT);  digitalWrite(LCD_CS,HIGH);
    pinMode(LCD_DC,OUTPUT);  digitalWrite(LCD_DC,HIGH);
    pinMode(LCD_RST,OUTPUT); digitalWrite(LCD_RST,HIGH);
    pinMode(LCD_BL,OUTPUT);  digitalWrite(LCD_BL,HIGH);
    for (int p : BPINS) pinMode(p, INPUT_PULLUP);

    SPI1.setTX(LCD_MOSI); SPI1.setSCK(LCD_SCK); SPI1.begin();
    SPI1.beginTransaction(_spi);

    digitalWrite(LCD_RST,LOW); delay(10); digitalWrite(LCD_RST,HIGH); delay(120);

    _cmd(0x01); delay(150);
    _cmd(0x11); delay(500);
    _cmd(0x3A); _dat(0x05);
    _cmd(0x36); _dat(0x70);
    _cmd(0xB2); _dat(0x0C);_dat(0x0C);_dat(0x00);_dat(0x33);_dat(0x33);
    _cmd(0xB7); _dat(0x35);
    _cmd(0xBB); _dat(0x19);
    _cmd(0xC0); _dat(0x2C);
    _cmd(0xC2); _dat(0x01);
    _cmd(0xC3); _dat(0x12);
    _cmd(0xC4); _dat(0x20);
    _cmd(0xC6); _dat(0x0F);
    _cmd(0xD0); _dat(0xA4); _dat(0xA1);
    _cmd(0x21); delay(10);
    _cmd(0x13); delay(10);
    _cmd(0x29); delay(20);

    // Clear framebuffer and screen
    memset(FB, 0, sizeof(FB));
    _win(0,0,LCD_W-1,LCD_H-1);
    uint32_t n = (uint32_t)LCD_W*LCD_H;
    digitalWrite(LCD_DC,HIGH); digitalWrite(LCD_CS,LOW);
    for (uint32_t i=0;i<n;i++){SPI1.transfer(0);SPI1.transfer(0);}
    digitalWrite(LCD_CS,HIGH);

    SPI1.endTransaction();
}