#include "lcd-menu.c"
#include "radioController.h"
#include "sdcard-mp3.h"
#include "talking_clock.h"
#include <sys/time.h>
#include "goertzel.h"

// Clock timer
TimerHandle_t timer_1_sec;
void time1SecCallback(TimerHandle_t xTimer);

/*
This file is to work out the onClicks, onExit, onEnter and update functions of the lcd-menu's. 

(This file is an extension of the lcd-menu.c file, so that is why there is a ".c" file included)
*/

//Main menu
void onClickMainEcho(i2c_lcd1602_info_t* lcd_info)
{
    displayMenu(lcd_info, ECHO_MENU_ID);
}

void onClickMainRadio(i2c_lcd1602_info_t* lcd_info)
{
    displayMenu(lcd_info, RADIO_MENU_ID);
}

void onClickMainClock(i2c_lcd1602_info_t* lcd_info)
{
    displayMenu(lcd_info, CLOCK_MENU_ID);
}

//Echo menu
void onClickEchoSpeech(i2c_lcd1602_info_t* lcd_info)
{
    displayMenu(lcd_info, SPEECH_MENU_ID);
}


//Radio menu
void onEnterRadio()
{
    printf("Entered the radio menu\n");
    xTaskCreate(&radio_task, "radio_task", 1024 * 3, NULL, 8, NULL);
}

void onExitRadio()
{
    printf("Exited the radio menu\n");
    radio_quit();
}

void onClickRadio538()
{
    radio_switch(lcdMenus[RADIO_MENU_ID].items[0].text);
}

void onClickRadioQ()
{
    radio_switch(lcdMenus[RADIO_MENU_ID].items[1].text);
}

void onClickRadioSky()
{
    radio_switch(lcdMenus[RADIO_MENU_ID].items[2].text);
} 

//Klok menu
void onEnterClock()
{
    printf("Entered the radio menu\n");
    xTaskCreate(&mp3_task, "radio_task", 1024 * 3, NULL, 8, NULL);

    vTaskDelay(2000/portTICK_RATE_MS);

    talkingClock_fillQueue();
    timer_1_sec = xTimerCreate("MyTimer", pdMS_TO_TICKS(1000), pdTRUE, (void *)1, &time1SecCallback);
    if (xTimerStart(timer_1_sec, 10) != pdPASS)
    {
        printf("Cannot start 1 second timer");
    }
}

void onExitClock()
{
    printf("Exited the radio menu\n");
    mp3_stopTask();
}

void onUpdateClock(void *p)
{
    strcpy(lcdMenus[CLOCK_MENU_ID].items[0].text, (char*) p);
}

void onClickClock()
{
    talkingClock_fillQueue();
}

//Speech menu
void onEnterSpeech()
{
    goertzel_start();
}

void onUpdateSpeech(void *p)
{
    strcpy(lcdMenus[SPEECH_MENU_ID].items[0].text, (char*) p);
}

void onExitSpeech()
{
    goertzel_stop();
}

void time1SecCallback(TimerHandle_t xTimer)
{
    // Print current time to the screen
    time_t now;
    struct tm timeinfo;
    time(&now);

    char strftime_buf[20];
    localtime_r(&now, &timeinfo);
    sprintf(&strftime_buf[0], "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    menu_updateMenu(menu_getLcdInfo(), (void*) strftime_buf);
    size_t timeSize = strlen(strftime_buf);

    // Say the time every hour
    // if (timeinfo.tm_sec == 0 && timeinfo.tm_min % 10 == 0)
    // {
    //     talkingClock_fillQueue();
    // }
}