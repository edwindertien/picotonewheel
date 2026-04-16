#pragma once
// ============================================================
//  lcd.h  –  Public interface for LCD display + buttons
//  All implementation is in lcd.cpp
// ============================================================
#include <stdint.h>
#include <stdbool.h>

// ---- Initialise LCD and buttons ----------------------------
void lcd_begin();

// ---- Mark regions dirty (call whenever state changes) ------
void ui_mark_dirty_top();
void ui_mark_dirty_bars();
void ui_mark_dirty_bottom();
void ui_mark_all_dirty();

// ---- Flush dirty tiles to screen ---------------------------
// Pass current state; call at ~30Hz from loop()
struct TonewheelManager;   // forward declaration
void ui_flush(const TonewheelManager& organ);

// ---- Handle joystick/button input --------------------------
void ui_handle_buttons(TonewheelManager& organ);

// ---- Notify of MIDI activity (flashes indicator) -----------
void ui_midi_activity();