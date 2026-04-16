// ============================================================
//  lcd.cpp  –  LCD driver + UI  (Waveshare Pico-LCD-1.14)
//  ST7789V  240×135  RGB565  SPI1
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
#define LCD_W   240
#define LCD_H   135
#define TILE_H   15     // rows per tile  (135/15 = 9 tiles exactly)
#define N_TILES   9

// ---- Layout ------------------------------------------------
// Top bar
#define TOP_Y      0
#define TOP_H     18

// Drawbar area
#define BAR_AREA_Y  19
#define BAR_AREA_H  90    // y=19..108
#define BAR_PAD_TOP  4    // blank pixels above bars
#define BAR_PAD_BOT 10    // space for footage label
#define BAR_H       (BAR_AREA_H - BAR_PAD_TOP - BAR_PAD_BOT)  // 76px
#define BAR_TOP_ABS (BAR_AREA_Y + BAR_PAD_TOP)                 // abs y=23
#define LABEL_Y_ABS (BAR_AREA_Y + BAR_AREA_H - BAR_PAD_BOT + 1) // abs y=100

#define N_BARS   9
#define BAR_W   22
#define BAR_GAP  3
#define BAR_X0   9       // left margin (9 + 9×22 + 8×3 + 9 = 240 ✓)

// Status bar
#define STAT_Y  110
#define STAT_H   25      // y=110..134

// ---- Colours  RGB565 ---------------------------------------
#define C_BLACK      0x0000
#define C_WHITE      0xFFFF
#define C_DARKBG     0x1082   // ~rgb(16,16,16)
#define C_TOPBG      0x2104   // ~rgb(32,32,32)
#define C_STATBG     0x2104
#define C_GRAY       0x4A49   // ~rgb(72,72,72)
#define C_LGRAY      0x8410   // ~rgb(128,128,128)
#define C_AMBER      0xFD00   // rgb(255,160,0)  — bar fill
#define C_AMBER_SEL  0xFFFF   // white for selected bar
#define C_AMBER_DIM  0x3880   // very dark amber — empty bar
#define C_DIM_SEL    0x2104   // dark grey — empty selected bar
#define C_GREEN      0x07E0   // MIDI / voice indicator
#define C_GREEN_DIM  0x0320   // dim green
#define C_ORANGE     0xFB00   // percussion
#define C_RED        0xD8C0   // click
#define C_BLUE       0x055F   // volume
#define C_CYAN       0x07FF   // info

// ---- Tile buffer -------------------------------------------
static uint16_t _buf[LCD_W * TILE_H];

static void b_fill(int x, int ty, int w, int h, uint16_t c) {
    for (int row = ty; row < ty+h && row < TILE_H; row++)
        for (int col = x; col < x+w && col < LCD_W; col++)
            _buf[row*LCD_W + col] = c;
}

// ---- 5×7 font (scale 1 or 2) --------------------------------
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

static void b_char(int x, int ty, char c, uint16_t fg, uint16_t bg, int sc=1) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t* g = FONT[c-32];
    for (int col=0; col<5; col++) {
        for (int row=0; row<7; row++) {
            uint16_t pix = (g[col]>>row)&1 ? fg : bg;
            for (int sy=0; sy<sc; sy++)
                for (int sx=0; sx<sc; sx++) {
                    int px=x+col*sc+sx, py=ty+row*sc+sy;
                    if (px>=0&&px<LCD_W&&py>=0&&py<TILE_H) _buf[py*LCD_W+px]=pix;
                }
        }
    }
}
static void b_str(int x, int ty, const char* s, uint16_t fg, uint16_t bg, int sc=1) {
    for (; *s; s++, x+=sc*6) b_char(x, ty, *s, fg, bg, sc);
}

// Draw a small "level dot bar" — n filled squares out of max, left→right
// Each square is sqsz×sqsz with 1px gap
static void b_dotbar(int x, int ty, int val, int maxval, int sqsz,
                     uint16_t fg, uint16_t bg) {
    for (int i=0; i<maxval; i++) {
        uint16_t c = (i < val) ? fg : bg;
        b_fill(x + i*(sqsz+1), ty, sqsz, sqsz, c);
    }
}

// Horizontal level bar (filled rect proportional to val/maxval)
static void b_hbar(int x, int ty, int w, int h, int val, int maxval,
                   uint16_t fg, uint16_t bg) {
    int filled = (maxval > 0) ? (val * w / maxval) : 0;
    if (filled > 0) b_fill(x, ty, filled, h, fg);
    if (filled < w) b_fill(x+filled, ty, w-filled, h, bg);
}

// ---- SPI / ST7789 ------------------------------------------
static SPISettings _spi(62500000, MSBFIRST, SPI_MODE0);

static void _cmd(uint8_t c) {
    digitalWrite(LCD_DC,LOW); digitalWrite(LCD_CS,LOW);
    SPI1.transfer(c);
    digitalWrite(LCD_CS,HIGH);
}
static void _dat(uint8_t d) {
    digitalWrite(LCD_DC,HIGH); digitalWrite(LCD_CS,LOW);
    SPI1.transfer(d);
    digitalWrite(LCD_CS,HIGH);
}
static void _win(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1) {
    // ST7789 RAM is 240x320; the 240x135 display sits at offset col+40, row+53
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
static void _push(uint16_t y0) {
    uint16_t y1 = y0+TILE_H-1; if(y1>=LCD_H) y1=LCD_H-1;
    _win(0,y0,LCD_W-1,y1);
    uint32_t n = (uint32_t)(y1-y0+1)*LCD_W;
    digitalWrite(LCD_DC,HIGH); digitalWrite(LCD_CS,LOW);
    for (uint32_t i=0;i<n;i++) { SPI1.transfer(_buf[i]>>8); SPI1.transfer(_buf[i]); }
    digitalWrite(LCD_CS,HIGH);
}

// ---- State -------------------------------------------------
static bool   _dirty[N_TILES];
static int    _sel   = 2;     // selected drawbar 0-8
static bool   _bp[7] = {};    // button previous
static bool   _bc[7] = {};    // button current
static const int BPINS[7] = {BTN_UP,BTN_DOWN,BTN_LEFT,BTN_RIGHT,BTN_PRESS,BTN_A,BTN_B};
static uint32_t _midi_until = 0;

// Footage labels
static const char* FOOTAGE[9] = {"16","5.3","8 ","4  ","2.7","2  ","1.6","1.3","1  "};

// ---- Tile renderer -----------------------------------------
static void _draw(int t, const TonewheelManager& org) {
    int y0 = t * TILE_H;
    b_fill(0, 0, LCD_W, TILE_H, C_BLACK);

    // =========================================================
    // TOP BAR  (tiles 0..1, abs y 0..17)
    // =========================================================
    for (int ay = TOP_Y; ay < TOP_Y+TOP_H; ay++) {
        if (ay < y0 || ay >= y0+TILE_H) continue;
        int ty = ay - y0;
        b_fill(0, ty, LCD_W, 1, C_TOPBG);
    }

    // Content only if tile overlaps top bar
    if (y0 < TOP_Y+TOP_H && y0+TILE_H > TOP_Y) {
        int ty_base = TOP_Y + 5 - y0;  // baseline for text in tile coords

        if (ty_base >= 0 && ty_base < TILE_H) {
            // Preset name
            const char* name = "Custom";
            const uint8_t* l = org.drawbars.level;
            if (l[0]==8&&l[1]==8&&l[2]==8&&l[3]==0&&l[4]==0&&l[5]==0&&l[6]==0&&l[7]==0&&l[8]==0) name="Full organ";
            else if (l[0]==8&&l[1]==6&&l[2]==8&&l[3]==8&&l[4]==0&&l[5]==0&&l[6]==0&&l[7]==0&&l[8]==0) name="Jazz";
            else if (l[0]==0&&l[1]==0&&l[2]==4&&l[3]==0&&l[4]==2&&l[5]==0&&l[6]==0&&l[7]==0&&l[8]==0) name="Flute";
            b_str(6, ty_base, name, C_WHITE, C_TOPBG, 1);

            // Selected drawbar value
            char buf[12];
            snprintf(buf, sizeof(buf), "DB%d  %d/8", _sel+1, l[_sel]);
            b_str(90, ty_base, buf, C_AMBER, C_TOPBG, 1);

            // Voice count
            snprintf(buf, sizeof(buf), "%d/%dv", org.activeCount(), MAX_ACTIVE_VOICES);
            b_str(190, ty_base, buf, C_GREEN, C_TOPBG, 1);

            // MIDI flash dot (8×8 rounded-ish by fill)
            uint16_t dot = (millis() < _midi_until) ? C_GREEN : C_GREEN_DIM;
            b_fill(224, ty_base, 8, 8, dot);
            // round corners: clear 4 pixels
            _buf[(ty_base)*LCD_W+224] = C_TOPBG;
            _buf[(ty_base)*LCD_W+231] = C_TOPBG;
            if (ty_base+7 < TILE_H) {
                _buf[(ty_base+7)*LCD_W+224] = C_TOPBG;
                _buf[(ty_base+7)*LCD_W+231] = C_TOPBG;
            }
        }
    }

    // =========================================================
    // DRAWBAR BARS  (abs y BAR_AREA_Y..BAR_AREA_Y+BAR_AREA_H)
    // =========================================================
    for (int bar = 0; bar < N_BARS; bar++) {
        int bx  = BAR_X0 + bar*(BAR_W+BAR_GAP);
        int lev = org.drawbars.level[bar];   // 0..8
        int fill_h    = lev * BAR_H / 8;
        int fill_top  = BAR_TOP_ABS + (BAR_H - fill_h);  // abs y where fill starts
        bool sel      = (bar == _sel);

        // Draw each abs pixel row of this bar
        for (int ay = BAR_TOP_ABS; ay < BAR_TOP_ABS+BAR_H; ay++) {
            if (ay < y0 || ay >= y0+TILE_H) continue;
            int ty = ay - y0;
            uint16_t col;
            if (ay >= fill_top) {
                col = sel ? C_AMBER_SEL : C_AMBER;
            } else {
                col = sel ? C_DIM_SEL : C_AMBER_DIM;
            }
            b_fill(bx, ty, BAR_W, 1, col);
        }

        // Thin separator between fill and empty (1px bright line at fill_top-1)
        if (lev > 0 && lev < 8) {
            int sep_abs = fill_top - 1;
            if (sep_abs >= y0 && sep_abs < y0+TILE_H) {
                uint16_t sc = sel ? C_WHITE : C_AMBER;
                b_fill(bx, sep_abs-y0, BAR_W, 1, sc);
            }
        }

        // Level number above bar (small, 1px font, centred)
        // Show as digit 0-8, abs y = BAR_TOP_ABS - 9
        int num_abs = BAR_TOP_ABS - 9;
        if (num_abs >= y0 && num_abs < y0+TILE_H) {
            char d[2] = {'0'+(char)lev, 0};
            uint16_t nc = sel ? C_WHITE : (lev>0 ? C_AMBER : C_GRAY);
            b_char(bx + BAR_W/2 - 2, num_abs-y0, d[0], nc, C_BLACK, 1);
        }

        // Footage label below bar
        int lab_abs = LABEL_Y_ABS;
        if (lab_abs >= y0 && lab_abs < y0+TILE_H) {
            uint16_t lc = sel ? C_WHITE : C_GRAY;
            b_str(bx, lab_abs-y0, FOOTAGE[bar], lc, C_BLACK, 1);
        }
    }

    // =========================================================
    // STATUS BAR  (abs y STAT_Y..STAT_Y+STAT_H)
    // =========================================================
    for (int ay = STAT_Y; ay < STAT_Y+STAT_H; ay++) {
        if (ay < y0 || ay >= y0+TILE_H) continue;
        int ty = ay - y0;
        b_fill(0, ty, LCD_W, 1, C_STATBG);
    }

    // Draw status content if tile overlaps status bar
    if (y0+TILE_H > STAT_Y && y0 < STAT_Y+STAT_H) {
        // Divider line at STAT_Y
        if (STAT_Y >= y0 && STAT_Y < y0+TILE_H)
            b_fill(0, STAT_Y-y0, LCD_W, 1, C_GRAY);

        int base = STAT_Y + 5 - y0;  // text baseline in tile coords
        int ibase = STAT_Y + 4 - y0; // icon baseline

        // ---- MIDI indicator (x=4..19) ----------------------
        if (base >= 0 && base < TILE_H) {
            b_str(4, base, "M", C_CYAN, C_STATBG, 1);
            uint16_t mc = (millis() < _midi_until) ? C_GREEN : C_GREEN_DIM;
            if (ibase+1 >= 0 && ibase+1 < TILE_H) b_fill(14, ibase+1, 5, 5, mc);
        }

        // ---- PERCUSSION (x=24..83) -------------------------
        if (base >= 0 && base < TILE_H) {
            // "P" label
            uint16_t pc = org.perc.enabled ? C_ORANGE : C_GRAY;
            b_str(24, base, "P", pc, C_STATBG, 1);

            // on/off indicator square
            if (ibase >= 0 && ibase < TILE_H) {
                b_fill(33, ibase, 10, 10, org.perc.enabled ? C_ORANGE : C_DARKBG);
                if (!org.perc.enabled) b_fill(34, ibase+1, 8, 8, C_STATBG); // hollow
            }

            // harmonic label: "2" or "3"
            if (base >= 0 && base < TILE_H) {
                char hc[2] = {(char)('0' + (org.perc.thirdHarmonic ? 3 : 2)), 0};
                b_str(46, base, hc, pc, C_STATBG, 1);
            }

            // fast/slow dot
            if (ibase+2 >= 0 && ibase+2 < TILE_H)
                b_fill(54, ibase+2, 6, 6, org.perc.fast ? C_ORANGE : C_GRAY);

            // soft/normal dot (smaller)
            if (ibase+3 >= 0 && ibase+3 < TILE_H)
                b_fill(63, ibase+3, 4, 4, org.perc.soft ? C_GRAY : C_ORANGE);
        }

        // ---- CLICK  (x=84..143) ----------------------------
        if (base >= 0 && base < TILE_H) {
            b_str(84, base, "C", C_RED, C_STATBG, 1);
            // 9-dot bar for click level 0..127 mapped to 0..9 dots
            if (ibase+1 >= 0 && ibase+1 < TILE_H) {
                int cdots = org.click.level * 9 / 127;
                b_dotbar(94, ibase+1, cdots, 9, 4, C_RED, C_DARKBG);
            }
        }

        // ---- VOLUME placeholder  (x=144..203) --------------
        // Master volume not implemented yet — show as full bar placeholder
        if (base >= 0 && base < TILE_H) {
            b_str(144, base, "V", C_BLUE, C_STATBG, 1);
            if (ibase+2 >= 0 && ibase+2 < TILE_H)
                b_hbar(154, ibase+2, 44, 6, 8, 8, C_BLUE, C_DARKBG);
        }

        // ---- VOICE COUNT  (x=204..239) ---------------------
        if (base >= 0 && base < TILE_H) {
            char vb[8]; snprintf(vb, sizeof(vb), "%d/%d", org.activeCount(), MAX_ACTIVE_VOICES);
            uint16_t vc = (org.activeCount() >= MAX_ACTIVE_VOICES) ? C_RED : C_GREEN;
            b_str(204, base, vb, vc, C_STATBG, 2);
            b_str(218, base+6, "voc", C_LGRAY, C_STATBG, 1);
        }
    }
}

// ---- Public API --------------------------------------------
void ui_mark_dirty_top()    { _dirty[0]=true; _dirty[1]=true; }
void ui_mark_dirty_bars()   { for(int i=1;i<N_TILES-1;i++) _dirty[i]=true; }
void ui_mark_dirty_bottom() { _dirty[N_TILES-2]=true; _dirty[N_TILES-1]=true; }
void ui_mark_all_dirty()    { for(int i=0;i<N_TILES;i++) _dirty[i]=true; }
void ui_midi_activity()     { _midi_until=millis()+120; _dirty[0]=true; _dirty[N_TILES-1]=true; }

void ui_flush(const TonewheelManager& org) {
    SPI1.beginTransaction(_spi);
    for (int t=0; t<N_TILES; t++) {
        if (!_dirty[t]) continue;
        _draw(t, org);
        _push(t*TILE_H);
        _dirty[t] = false;
    }
    SPI1.endTransaction();
}

void ui_handle_buttons(TonewheelManager& org) {
    for (int i=0;i<7;i++) { _bp[i]=_bc[i]; _bc[i]=!digitalRead(BPINS[i]); }
    auto edge=[](int i){ return _bc[i]&&!_bp[i]; };

    if (edge(2)) { if(_sel>0){_sel--;ui_mark_dirty_bars();ui_mark_dirty_top();} }   // LEFT
    if (edge(3)) { if(_sel<8){_sel++;ui_mark_dirty_bars();ui_mark_dirty_top();} }   // RIGHT
    if (edge(0)) {  // UP
        uint8_t& lv=org.drawbars.level[_sel];
        if(lv<8){lv++;org.propagateDrawbars();ui_mark_dirty_bars();ui_mark_dirty_top();}
    }
    if (edge(1)) {  // DOWN
        uint8_t& lv=org.drawbars.level[_sel];
        if(lv>0){lv--;org.propagateDrawbars();ui_mark_dirty_bars();ui_mark_dirty_top();}
    }
    if (edge(4)) {  // PRESS: cycle preset
        static int pr=0; pr=(pr+1)%3;
        if(pr==0) org.drawbars.presetFullOrgan();
        else if(pr==1) org.drawbars.presetJazz();
        else org.drawbars.presetFlute();
        org.propagateDrawbars(); ui_mark_all_dirty();
    }
    if (edge(5)) { org.perc.enabled=!org.perc.enabled; ui_mark_dirty_bottom(); ui_mark_dirty_top(); }  // A
    if (edge(6)) { org.perc.thirdHarmonic=!org.perc.thirdHarmonic; ui_mark_dirty_bottom(); }           // B
}

void lcd_begin() {
    pinMode(LCD_CS,OUTPUT);  digitalWrite(LCD_CS,HIGH);
    pinMode(LCD_DC,OUTPUT);  digitalWrite(LCD_DC,HIGH);
    pinMode(LCD_RST,OUTPUT); digitalWrite(LCD_RST,HIGH);
    pinMode(LCD_BL,OUTPUT);  digitalWrite(LCD_BL,HIGH);
    for(int p:BPINS) pinMode(p,INPUT_PULLUP);

    SPI1.setTX(LCD_MOSI); SPI1.setSCK(LCD_SCK); SPI1.begin();
    SPI1.beginTransaction(_spi);

    digitalWrite(LCD_RST,LOW); delay(10); digitalWrite(LCD_RST,HIGH); delay(120);

    _cmd(0x01); delay(150);
    _cmd(0x11); delay(500);
    _cmd(0x3A); _dat(0x05);                                      // RGB565
    _cmd(0x36); _dat(0x70);                                      // landscape
    _cmd(0xB2); _dat(0x0C);_dat(0x0C);_dat(0x00);_dat(0x33);_dat(0x33);
    _cmd(0xB7); _dat(0x35);
    _cmd(0xBB); _dat(0x19);
    _cmd(0xC0); _dat(0x2C);
    _cmd(0xC2); _dat(0x01);
    _cmd(0xC3); _dat(0x12);
    _cmd(0xC4); _dat(0x20);
    _cmd(0xC6); _dat(0x0F);
    _cmd(0xD0); _dat(0xA4); _dat(0xA1);
    _cmd(0x21); delay(10);   // inversion ON  (required for this display)
    _cmd(0x13); delay(10);   // normal display on
    _cmd(0x29); delay(20);

    // Clear screen
    _win(0,0,LCD_W-1,LCD_H-1);
    uint32_t n=(uint32_t)LCD_W*LCD_H;
    digitalWrite(LCD_DC,HIGH); digitalWrite(LCD_CS,LOW);
    for(uint32_t i=0;i<n;i++){SPI1.transfer(0);SPI1.transfer(0);}
    digitalWrite(LCD_CS,HIGH);

    SPI1.endTransaction();
    for(int i=0;i<N_TILES;i++) _dirty[i]=true;
}