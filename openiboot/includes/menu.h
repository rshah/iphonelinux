#ifndef MENU_H
#define MENU_H

#include "openiboot.h"
typedef struct MenuImage {
    uint32_t size;
    uint32_t *image;
    uint16_t x;
    uint16_t y;
    int w;
    int h;
} MenuImage;

typedef struct MenuItem {
    uint8_t osId;
    MenuImage imageNormal;
    MenuImage imageFocused;
    MenuImage bigImage;
} MenuItem;

typedef struct MenuTheme {
    uint8_t majorVersion;
    uint8_t minorVersion;
    uint32_t backgroundSize;
    uint32_t *background;
    uint8_t totalMenu;
    MenuItem **menus;
} MenuTheme;

int menu_setup(int timeout);

#endif
