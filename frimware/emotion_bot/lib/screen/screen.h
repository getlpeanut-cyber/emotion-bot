#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>

// Forward declaration, avoid including the full driver header here
namespace esp_panel { namespace drivers { class LCD; } }

void screen_init();
void screen_fill(uint16_t color);
void screen_test();
void screen_color_bar_test();

// Get the current LCD panel pointer for the LVGL port layer
esp_panel::drivers::LCD *screen_get_lcd();

#endif
