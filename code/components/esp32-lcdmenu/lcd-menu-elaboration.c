#include "lcd-menu.c"
#include "radioController.h"

/*
This file is to work out the onClicks, onExit, onEnter and update functions of the lcd-menu's. 

(This file is an extension of the lcd-menu.c file, so thats why we include a ".c" file, sorry)
*/

//Main menu
void onClickMainEcho(i2c_lcd1602_info_t* lcd_info)
{
    displayMenu(lcd_info, ECHO_MENU_ID);
}

void onClickMainRadio(i2c_lcd1602_info_t* lcd_info)
{
    displayMenu(lcd_info, RADIO_MENU_ID);
    radioSwitch(lcdMenus[RADIO_MENU_ID].items[0].text);
}

void onClickMainClock(i2c_lcd1602_info_t* lcd_info)
{
    displayMenu(lcd_info, CLOCK_MENU_ID);
}


//Radio menu
void onEnterRadio()
{
    printf("Entered the radio menu\n");
}

void onExitRadio()
{
    printf("Exited the radio menu\n");
}

//Klok menu
void onEnterClock()
{
    printf("Entered the radio menu\n");
}

void onExitClock()
{
    printf("Exited the radio menu\n");
}

void onUpdateClock(void *p)
{
    strcpy(lcdMenus[CLOCK_MENU_ID].items[0].text, (char*) p);
}