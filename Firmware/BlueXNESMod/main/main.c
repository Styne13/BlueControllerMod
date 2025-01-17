//
//  BlueXNESMod Firmware
//
//  Created by Nathan Reeves 2019
//  edited by Styne13 2021 to add NES/SNES support

#include "esp_log.h"
#include "esp_hidd_api.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_bt.h"
#include "esp_err.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_gap_bt_api.h"
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "driver/rmt.h"
#include "driver/periph_ctrl.h"
#include "soc/rmt_reg.h"

//Controler Type defines
#define NES
//#define SNES
//#define DEBUG

#define LED_GPIO    25
#define PIN_SEL  (1ULL<<LED_GPIO)

//for reading GameCube controller values
#define RMT_TX_GPIO_NUM  23     // GameCube TX GPIO ----
#define RMT_RX_GPIO_NUM  18     // GameCube RX GPIO ----
#define RMT_TX_CHANNEL    2     /*!< RMT channel for transmitter */
#define RMT_RX_CHANNEL    3     /*!< RMT channel for receiver */
#define RMT_CLK_DIV      80    /*!< RMT counter clock divider */
#define RMT_TICK_8_US    (80000000/RMT_CLK_DIV/80000)   /*!< RMT counter value for 10 us.(Source clock is APB clock) */
#define RMT_TICK_10_US    (80000000/RMT_CLK_DIV/100000)   /*!< RMT counter value for 10 us.(Source clock is APB clock) */
#define RMT_TICK_12_US    (80000000/RMT_CLK_DIV/120000)   /*!< RMT counter value for 10 us.(Source clock is APB clock) */
#define RMT_TICK_100_US    (80000000/RMT_CLK_DIV/1000000)   /*!< RMT counter value for 10 us.(Source clock is APB clock) */
#define rmt_item32_tIMEOUT_US  9500   /*!< RMT receiver timeout value(us) */

#define XNES_LATCH 13
#define XNES_CLOCK 14
#define XNES_DATA 15
#define LOW 0
#define HIGH 1

//button defines
/* NES:
       Keys: | A B Select Start Up Down Left Right |
       Bits: | 0 1   2      3    4   5    6    7   |

       SNES
       Keys: | B Y Select Start Up Down Left Right A X  L  R N/A N/A N/A N/A |
       Bits: | 0 1   2      3    4   5    6    7   8 9 10 11  12  13  14  15 |
*/
#ifdef NES
    #define BTN_A 0x01
    #define BTN_B 0x02
    #define BTN_SELECT 0x04
    #define BTN_START 0x08
    #define BTN_UP 0x10
    #define BTN_DOWN 0x20
    #define BTN_LEFT 0x40
    #define BTN_RIGHT 0x80
    #define READ_LOOP_MAX 8
#endif
#ifdef SNES
    #define BTN_A 0x100
    #define BTN_B 0x01
    #define BTN_SELECT 0x04
    #define BTN_START 0x08
    #define BTN_UP 0x10
    #define BTN_DOWN 0x20
    #define BTN_LEFT 0x40
    #define BTN_RIGHT 0x80
    #define BTN_X 0x200  
    #define BTN_Y 0x02
    #define BTN_R 0x800
    #define BTN_L 0x400
    #define READ_LOOP_MAX 16    
#endif
bool latched;  

//Calibration
static int lxcalib = 0;
static int lycalib = 0;
static int cxcalib = 0;
static int cycalib = 0;
//Buttons and sticks
static uint8_t but1_send = 0;
static uint8_t but2_send = 0;
static uint8_t but3_send = 0;
static uint8_t lx_send = 0;
static uint8_t ly_send = 0;
static uint8_t cx_send = 0;
static uint8_t cy_send = 0;
static uint8_t lt_send = 0;
static uint8_t rt_send = 0;

//RMT Transmitter Init - for reading GameCube controller
rmt_item32_t items[25];
rmt_config_t rmt_tx;

SemaphoreHandle_t xSemaphore;
bool connected = false;
int paired = 0;
TaskHandle_t SendingHandle = NULL;
TaskHandle_t BlinkHandle = NULL;
uint8_t timer = 0;
static void rmt_tx_init()
{
    
    rmt_tx.channel = RMT_TX_CHANNEL;
    rmt_tx.gpio_num = RMT_TX_GPIO_NUM;
    rmt_tx.mem_block_num = 1;
    rmt_tx.clk_div = RMT_CLK_DIV;
    rmt_tx.tx_config.loop_en = false;
    rmt_tx.tx_config.carrier_freq_hz = 24000000;
    rmt_tx.tx_config.carrier_level = 1;
    rmt_tx.tx_config.carrier_en = 0;
    rmt_tx.tx_config.idle_level = 1;
    rmt_tx.tx_config.idle_output_en = true;
    rmt_tx.rmt_mode = 0;
    rmt_config(&rmt_tx);
    rmt_driver_install(rmt_tx.channel, 0, 0);
    
    //Fill items[] with console->controller command: 0100 0000 0000 0011 0000 0010
    
    items[0].duration0 = 3;
    items[0].level0 = 0;
    items[0].duration1 = 1;
    items[0].level1 = 1;
    items[1].duration0 = 1;
    items[1].level0 = 0;
    items[1].duration1 = 3;
    items[1].level1 = 1;
    int j;
    for(j = 0; j < 12; j++) {
        items[j+2].duration0 = 3;
        items[j+2].level0 = 0;
        items[j+2].duration1 = 1;
        items[j+2].level1 = 1;
    }
    items[14].duration0 = 1;
    items[14].level0 = 0;
    items[14].duration1 = 3;
    items[14].level1 = 1;
    items[15].duration0 = 1;
    items[15].level0 = 0;
    items[15].duration1 = 3;
    items[15].level1 = 1;
    for(j = 0; j < 8; j++) {
        items[j+16].duration0 = 3;
        items[j+16].level0 = 0;
        items[j+16].duration1 = 1;
        items[j+16].level1 = 1;
    }
    items[24].duration0 = 1;
    items[24].level0 = 0;
    items[24].duration1 = 3;
    items[24].level1 = 1;
    
}

static void xnes_init()
{
    gpio_reset_pin(XNES_LATCH);
    gpio_set_direction(XNES_LATCH, GPIO_MODE_OUTPUT);
    gpio_reset_pin(XNES_CLOCK);
    gpio_set_direction(XNES_CLOCK, GPIO_MODE_OUTPUT);
    gpio_reset_pin(XNES_DATA);
    gpio_set_direction(XNES_DATA, GPIO_MODE_INPUT);

    gpio_set_level(XNES_LATCH, LOW);
    latched = false;
}

static void xnes_get_buttons()
{
    ESP_LOGI("hi", "Started xnes_get_buttons from core %d!\n", xPortGetCoreID() );

    //button init values
    uint8_t but1 = 0;
    uint8_t but2 = 0;
    uint8_t but3 = 0;
    uint8_t lx = 0;
    uint8_t ly = 0;
    uint8_t cx = 0;
    uint8_t cy = 0;

    while(1)
    {
        but1 = 0;
        but2 = 0;
        but3 = 0;
        lx = 0;
        ly = 0;
        cx = 0;
        cy = 0;
        unsigned int fromController = 0x00;

        //Implement read function here
        gpio_set_level(XNES_LATCH, HIGH);
        vTaskDelay(6);
        gpio_set_level(XNES_LATCH, LOW);

        latched = true;

        //Wait for controller to start sending the button values
        vTaskDelay(6);
        for(int i = 0; i < READ_LOOP_MAX; i++) {
            //read the value, shift it and store it as a bit on fromController:
            fromController |= !gpio_get_level(XNES_DATA) << i;

            gpio_set_level(XNES_CLOCK, HIGH);
            vTaskDelay(6);
            gpio_set_level(XNES_CLOCK, LOW);
            vTaskDelay(6);
        }
        latched = false;
        #ifdef DEBUG
            //for debug purpose
            ESP_LOGI("hi", "fromController: %x\n", fromController);
            //vTaskDelay(RMT_TICK_100_US);
        #endif

        //transfere buttons to output
        if ((fromController & BTN_A) == BTN_A) but1 += 0x08;// A
        if ((fromController & BTN_B) == BTN_B) but1 += 0x04;// B
        
        #ifdef SNES
            //For SNES only
            if((fromController & BTN_X) == BTN_X) but1 += 0x02;// X
            if((fromController & BTN_Y) == BTN_Y) but1 += 0x01;// Y
        #endif
        
        // START/PLUS(SELECT)
        if((fromController & BTN_START) == BTN_START) but2 += 0x02;
        if((fromController & BTN_SELECT) == BTN_SELECT) but2 += 0x01;

        //DPAD
        if((fromController & BTN_LEFT) == BTN_LEFT) but3 += 0x08;// L
        if((fromController & BTN_RIGHT) == BTN_RIGHT) but3 += 0x04;// R
        if((fromController & BTN_DOWN) == BTN_DOWN) but3 += 0x01;// D
        if((fromController & BTN_UP) == BTN_UP) but3 += 0x02;// U

        #ifdef SNES
            //For SNES only
            //R/L TRIGGER
            if((fromController & BTN_R) == BTN_R) but1 += 0x80;// ZR
            if((fromController & BTN_L) == BTN_L) but3 += 0x80;// ZL
        #endif
        #ifdef NES
            //otherwise simulate ZR and ZL pressed to enter emu menu
            if (((fromController & BTN_START) == BTN_START ) & ((fromController & BTN_SELECT) == BTN_SELECT)) {
                but1 += 0x80;   // ZR
                but3 += 0x80;   // ZL
            }
        #endif

        //simulate home button
        if (((fromController & BTN_START) == BTN_START ) & ((fromController & BTN_SELECT) == BTN_SELECT) & ((fromController & BTN_RIGHT) == BTN_RIGHT)) {
            but2 = 0x10;    //HOME
        }

        // //simulate plus button
        // if (((fromController & BTN_START) == BTN_START ) & ((fromController & BTN_SELECT) == BTN_SELECT) & ((fromController & BTN_UP) == BTN_UP)) {
        //     but2 = 0x10;    //HOME
        // }

        //simulate minus button
        if (((fromController & BTN_START) == BTN_START ) & ((fromController & BTN_SELECT) == BTN_SELECT) & ((fromController & BTN_DOWN) == BTN_DOWN)) {
            but2 = 0x01;    //MINUS
        }
        

        but1_send = but1;
        but2_send = but2;
        but3_send = but3;
        //set sticks to middle position, because we don't have them, so no glitches will occur
        lx_send = 127;   //x + lxcalib;
        ly_send = 127;   //ly + lycalib;
        cx_send = 127;   //cx + cxcalib;
        cy_send = 127;   //cy + cycalib;
        lt_send = 0;//lt;//left trigger analog
        rt_send = 0;//rt;//right trigger analog
    }
}

//Switch button report example //         batlvl       Buttons              Lstick           Rstick
//static uint8_t report30[] = {0x30, 0x00, 0x90,   0x00, 0x00, 0x00,   0x00, 0x00, 0x00,   0x00, 0x00, 0x00};
static uint8_t report30[] = {
    0x30,
    0x0,
    0x80,
    0,//but1
    0,//but2
    0,//but3
    0,//Ls
    0,//Ls
    0,//Ls
    0,//Rs
    0,//Rs
    0,//Rs
    0x08
};
static uint8_t emptyReport[] = {
    0x0,
    0x0
};

void send_buttons()
{
    xSemaphoreTake(xSemaphore, portMAX_DELAY);
    report30[1] = timer;
    //buttons
    report30[3] = but1_send;
    report30[4] = but2_send;
    report30[5] = but3_send;
    //encode left stick
    report30[6] = (lx_send << 4) & 0xF0;
    report30[7] = (lx_send & 0xF0) >> 4;
    report30[8] = ly_send;
    //encode right stick
    report30[9] = (cx_send << 4) & 0xF0;
    report30[10] = (cx_send & 0xF0) >> 4;
    report30[11] = cy_send;
    xSemaphoreGive(xSemaphore);
    timer+=1;
    if(timer == 255)
        timer = 0;
    
    if(!paired)
    {
        emptyReport[1] = timer;
        esp_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, 0xa1, sizeof(emptyReport), emptyReport);
        vTaskDelay(100);
    }
    else
    {
        esp_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, 0xa1, sizeof(report30), report30);
        vTaskDelay(15);
    }
    
    
}
const uint8_t hid_descriptor_gamecube[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    //Padding
    0x95, 0x03,          //     REPORT_COUNT = 3
    0x75, 0x08,          //     REPORT_SIZE = 8
    0x81, 0x03,          //     INPUT = Cnst,Var,Abs
    //Sticks
    0x09, 0x30,        //   Usage (X)
    0x09, 0x31,        //   Usage (Y)
    0x09, 0x32,        //   Usage (Z)
    0x09, 0x35,        //   Usage (Rz)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x04,        //   Report Count (4)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    //DPAD
    0x09, 0x39,        //   Usage (Hat switch)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x07,        //   Logical Maximum (7)
    0x35, 0x00,        //   Physical Minimum (0)
    0x46, 0x3B, 0x01,  //   Physical Maximum (315)
    0x65, 0x14,        //   Unit (System: English Rotation, Length: Centimeter)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x42,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,Null State)
    //Buttons
    0x65, 0x00,        //   Unit (None)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (0x01)
    0x29, 0x0E,        //   Usage Maximum (0x0E)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x0E,        //   Report Count (14)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    //Padding
    0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
    0x09, 0x20,        //   Usage (0x20)
    0x75, 0x06,        //   Report Size (6)
    0x95, 0x01,        //   Report Count (1)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x7F,        //   Logical Maximum (127)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    //Triggers
    0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x33,        //   Usage (Rx)
    0x09, 0x34,        //   Usage (Ry)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x02,        //   Report Count (2)
    0x81, 0x02,
    0xc0
};
int hid_descriptor_gc_len = sizeof(hid_descriptor_gamecube);
///Switch Replies
static uint8_t reply02[] = {0x21, 0x01, 0x40, 0x00, 0x00, 0x00, 0xe6, 0x27, 0x78, 0xab, 0xd7, 0x76, 0x00, 0x82, 0x02, 0x03, 0x48, 0x03, 0x02, 0xD8, 0xA0, 0x1D, 0x40, 0x15, 0x66, 0x03, 0x00, 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00
    , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 };
static uint8_t reply08[] = {0x21, 0x02, 0x8E, 0x84, 0x00, 0x12, 0x01, 0x18, 0x80, 0x01, 0x18, 0x80, 0x80, 0x80, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00
    , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 };
static uint8_t reply03[] = {0x21, 0x05, 0x8E, 0x84, 0x00, 0x12, 0x01, 0x18, 0x80, 0x01, 0x18, 0x80, 0x80, 0x80, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00
    , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 };
static uint8_t reply04[] = {0x21, 0x06, 0x8E, 0x84, 0x00, 0x12, 0x01, 0x18, 0x80, 0x01, 0x18, 0x80, 0x80, 0x83, 0x04, 0x00, 0x6a, 0x01, 0xbb, 0x01, 0x93, 0x01, 0x95, 0x01, 0x00, 0x00, 0x00, 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00
    , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00};
static uint8_t reply1060[] = {0x21, 0x03, 0x8E, 0x84, 0x00, 0x12, 0x01, 0x18, 0x80, 0x01, 0x18, 0x80, 0x80, 0x90, 0x10, 0x00, 0x60, 0x00, 0x00, 0x10, 0x00, 0x00 , 0x00, 0x00, 0x00, 0x00, 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00
    , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 };
static uint8_t reply1050[] = { 0x21, 0x04, 0x8E, 0x84, 0x00, 0x12, 0x01, 0x18, 0x80, 0x01, 0x18, 0x80, 0x80, 0x90, 0x10, 0x50, 0x60, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00,0x00, 0x00, 0x00, 0x00, 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00
    , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 };
static uint8_t reply1080[] = {0x21, 0x04, 0x8E, 0x84, 0x00, 0x12, 0x01, 0x18, 0x80, 0x01, 0x18, 0x80, 0x80, 0x90, 0x10, 0x80, 0x60, 0x00, 0x00, 0x18, 0x5e, 0x01, 0x00, 0x00, 0xf1, 0x0f,
    0x19, 0xd0, 0x4c, 0xae, 0x40, 0xe1,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00};
static uint8_t reply1098[] = {0x21, 0x04, 0x8E, 0x84, 0x00, 0x12, 0x01, 0x18, 0x80, 0x01, 0x18, 0x80, 0x80, 0x90, 0x10, 0x98, 0x60, 0x00, 0x00, 0x12, 0x19, 0xd0, 0x4c, 0xae, 0x40, 0xe1,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00};
//User analog stick calib
static uint8_t reply1010[] = {0x21, 0x04, 0x8E, 0x84, 0x00, 0x12, 0x01, 0x18, 0x80, 0x01, 0x18, 0x80, 0x80, 0x90, 0x10, 0x10, 0x80, 0x00, 0x00, 0x18, 0x00, 0x00};
static uint8_t reply103D[] = {0x21, 0x05, 0x8E, 0x84, 0x00, 0x12, 0x01, 0x18, 0x80, 0x01, 0x18, 0x80, 0x80, 0x90, 0x10, 0x3D, 0x60, 0x00, 0x00, 0x19, 0xF0, 0x07, 0x7f, 0xF0, 0x07, 0x7f, 0xF0, 0x07, 0x7f, 0xF0, 0x07, 0x7f, 0xF0, 0x07, 0x7f, 0xF0, 0x07, 0x7f, 0xF0, 0x07, 0x7f, 0xF0, 0x07, 0x7f, 0x0f, 0x0f, 0x00, 0x00, 0x00, 0x00};
static uint8_t reply1020[] = {0x21, 0x04, 0x8E, 0x84, 0x00, 0x12, 0x01, 0x18, 0x80, 0x01, 0x18, 0x80, 0x80, 0x90, 0x10, 0x20, 0x60, 0x00, 0x00, 0x18, 0x00, 0x00};
static uint8_t reply4001[] = {0x21, 0x04, 0x8E, 0x84, 0x00, 0x12, 0x01, 0x18, 0x80, 0x01, 0x18, 0x80, 0x80, 0x80, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static uint8_t reply4801[] = {0x21, 0x04, 0x8E, 0x84, 0x00, 0x12, 0x01, 0x18, 0x80, 0x01, 0x18, 0x80, 0x80, 0x80, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static uint8_t reply3001[] = {0x21, 0x04, 0x8E, 0x84, 0x00, 0x12, 0x01, 0x18, 0x80, 0x01, 0x18, 0x80, 0x80, 0x80, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static uint8_t reply3333[] = {0x21, 0x03, 0x8E, 0x84, 0x00, 0x12, 0x01, 0x18, 0x80, 0x01, 0x18, 0x80, 0x80, 0x80, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 , 0x00, 0x00, 0x00, 0x00, 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00 , 0x00 , 0x00
    , 0x00 , 0x00 , 0x00 , 0x00  , 0x00 , 0x00, 0x00 };



// sending bluetooth values every 15ms
void send_task(void* pvParameters) {
    const char* TAG = "send_task";
    ESP_LOGI(TAG, "Sending hid reports on core %d\n", xPortGetCoreID() );
    while(1)
    {
        send_buttons();
    }
}

// callback for notifying when hidd application is registered or not registered
void application_cb(esp_bd_addr_t bd_addr, esp_hidd_application_state_t state) {
    const char* TAG = "application_cb";

    switch(state) {
        case ESP_HIDD_APP_STATE_NOT_REGISTERED:
            ESP_LOGI(TAG, "app not registered");
            break;
        case ESP_HIDD_APP_STATE_REGISTERED:
            ESP_LOGI(TAG, "app is now registered!");
            if(bd_addr == NULL) {
                ESP_LOGI(TAG, "bd_addr is null...");
                break;
            }
            break;
        default:
            ESP_LOGW(TAG, "unknown app state %i", state);
            break;
    }
}
//LED blink
void startBlink()
{
    while(1) {
        gpio_set_level(LED_GPIO, 0);
        vTaskDelay(150);
        gpio_set_level(LED_GPIO, 1);
        vTaskDelay(150);
        gpio_set_level(LED_GPIO, 0);
        vTaskDelay(150);
        gpio_set_level(LED_GPIO, 1);
        vTaskDelay(1000);
    }
    vTaskDelete(NULL);
}
// callback for hidd connection changes
void connection_cb(esp_bd_addr_t bd_addr, esp_hidd_connection_state_t state) {
    const char* TAG = "connection_cb";
    
    switch(state) {
        case ESP_HIDD_CONN_STATE_CONNECTED:
            ESP_LOGI(TAG, "connected to %02x:%02x:%02x:%02x:%02x:%02x",
                bd_addr[0], bd_addr[1], bd_addr[2], bd_addr[3], bd_addr[4], bd_addr[5]);
            ESP_LOGI(TAG, "setting bluetooth non connectable");
            esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);

            //clear blinking LED - solid
            vTaskDelete(BlinkHandle);
            BlinkHandle = NULL;
            gpio_set_level(LED_GPIO, 1);
            //start solid
            xSemaphoreTake(xSemaphore, portMAX_DELAY);
            connected = true;
            xSemaphoreGive(xSemaphore);
            //restart send_task
            if(SendingHandle != NULL)
            {
                vTaskDelete(SendingHandle);
                SendingHandle = NULL;
            }
            xTaskCreatePinnedToCore(send_task, "send_task", 2048, NULL, 2, &SendingHandle, 0);
            break;
        case ESP_HIDD_CONN_STATE_CONNECTING:
            ESP_LOGI(TAG, "connecting");
            break;
        case ESP_HIDD_CONN_STATE_DISCONNECTED:
            xTaskCreate(startBlink, "blink_task", 1024, NULL, 1, &BlinkHandle);
            //start blink
            ESP_LOGI(TAG, "disconnected from %02x:%02x:%02x:%02x:%02x:%02x",
                bd_addr[0], bd_addr[1], bd_addr[2], bd_addr[3], bd_addr[4], bd_addr[5]);
            ESP_LOGI(TAG, "making self discoverable");
            paired = 0;
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
            xSemaphoreTake(xSemaphore, portMAX_DELAY);
            connected = false;
            xSemaphoreGive(xSemaphore);
            break;
        case ESP_HIDD_CONN_STATE_DISCONNECTING:
            ESP_LOGI(TAG, "disconnecting");
            break;
        default:
            ESP_LOGI(TAG, "unknown connection status");
            break;
    }
}

//callback for discovering
void get_device_cb()
{
    ESP_LOGI("hi", "found a device");
}

// callback for when hid host requests a report
void get_report_cb(uint8_t type, uint8_t id, uint16_t buffer_size) {
    const char* TAG = "get_report_cb";
    ESP_LOGI(TAG, "got a get_report request from host");
}

// callback for when hid host sends a report
void set_report_cb(uint8_t type, uint8_t id, uint16_t len, uint8_t* p_data) {
    const char* TAG = "set_report_cb";
    ESP_LOGI(TAG, "got a report from host");
}

// callback for when hid host requests a protocol change
void set_protocol_cb(uint8_t protocol) {
    const char* TAG = "set_protocol_cb";
    ESP_LOGI(TAG, "got a set_protocol request from host");
}

// callback for when hid host sends interrupt data
void intr_data_cb(uint8_t report_id, uint16_t len, uint8_t* p_data) {
    const char* TAG = "intr_data_cb";
    //switch pairing sequence
    if(len == 49)
    {
    if(p_data[10] == 2)
    {
        esp_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, 0xa1, sizeof(reply02), reply02);
    }
    if(p_data[10] == 8)
    {
        esp_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, 0xa1, sizeof(reply08), reply08);
    }
    if(p_data[10] == 16 && p_data[11] == 0 && p_data[12] == 96)
    {
        esp_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, 0xa1, sizeof(reply1060), reply1060);
    }
    if(p_data[10] == 16 && p_data[11] == 80 && p_data[12] == 96)
    {
        esp_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, 0xa1, sizeof(reply1050), reply1050);
    }
    if(p_data[10] == 3)
    {
        esp_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, 0xa1, sizeof(reply03), reply03);
    }
    if(p_data[10] == 4)
    {
        esp_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, 0xa1, sizeof(reply04), reply04);
    }
    if(p_data[10] == 16 && p_data[11] == 128 && p_data[12] == 96)
    {
        esp_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, 0xa1, sizeof(reply1080), reply1080);
    }
    if(p_data[10] == 16 && p_data[11] == 152 && p_data[12] == 96)
    {
        esp_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, 0xa1, sizeof(reply1098), reply1098);
    }
    if(p_data[10] == 16 && p_data[11] == 16 && p_data[12] == 128)
    {
        esp_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, 0xa1, sizeof(reply1010), reply1010);
    }
    if(p_data[10] == 16 && p_data[11] == 61 && p_data[12] == 96)
    {
        esp_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, 0xa1, sizeof(reply103D), reply103D);
    }
    if(p_data[10] == 16 && p_data[11] == 32 && p_data[12] == 96)
    {
        esp_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, 0xa1, sizeof(reply1020), reply1020);
    }
    if(p_data[10] == 64 && p_data[11] == 1)
    {
        esp_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, 0xa1, sizeof(reply4001), reply4001);
    }
    if(p_data[10] == 72 && p_data[11] == 1)
    {
        esp_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, 0xa1, sizeof(reply4801), reply4801);
    }
    if(p_data[10] == 48 && p_data[11] == 1)
    {
        esp_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, 0xa1, sizeof(reply3001), reply3001);
    }
    
    if(p_data[10] == 33 && p_data[11] == 33)
    {
        esp_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, 0xa1, sizeof(reply3333), reply3333);
        paired = 1;
        
    }
    if(p_data[10] == 64 && p_data[11] == 2)
    {
        esp_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, 0xa1, sizeof(reply4001), reply4001);
    }
        //ESP_LOGI(TAG, "got an interrupt report from host, subcommand: %d  %d  %d Length: %d", p_data[10], p_data[11], p_data[12], len);
    }
    else
    {
        
        //ESP_LOGI("heap size:", "%d", xPortGetFreeHeapSize());
        //ESP_LOGI(TAG, "pairing packet size != 49, subcommand: %d  %d  %d  Length: %d", p_data[10], p_data[11], p_data[12], len);
    }
    
    
}

// callback for when hid host does a virtual cable unplug
void vc_unplug_cb(void) {
    const char* TAG = "vc_unplug_cb";
    ESP_LOGI(TAG, "host did a virtual cable unplug");
}

void set_bt_address()
{
    //store a random mac address in flash
    nvs_handle my_handle;
    esp_err_t err;
    uint8_t bt_addr[8];
    
    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;
    
    size_t addr_size = 0;
    err = nvs_get_blob(my_handle, "mac_addr", NULL, &addr_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    
    if (addr_size > 0) {
        err = nvs_get_blob(my_handle, "mac_addr", bt_addr, &addr_size);
    }
    else
    {
        for(int i=0; i<8; i++)
            bt_addr[i] = esp_random()%255;
        size_t addr_size = sizeof(bt_addr);
        err = nvs_set_blob(my_handle, "mac_addr", bt_addr, addr_size);
    }
    
    err = nvs_commit(my_handle);
    nvs_close(my_handle);
    esp_base_mac_addr_set(bt_addr);
    
    //put mac addr in switch pairing packet
    for(int z=0; z<6; z++)
        reply02[z+19] = bt_addr[z];
}
void print_bt_address() {
    const char* TAG = "bt_address";
    const uint8_t* bd_addr;

    bd_addr = esp_bt_dev_get_address();
    ESP_LOGI(TAG, "my bluetooth address is %02X:%02X:%02X:%02X:%02X:%02X",
        bd_addr[0], bd_addr[1], bd_addr[2], bd_addr[3], bd_addr[4], bd_addr[5]);
}

#define SPP_TAG "tag"
static void esp_bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch(event){
        case ESP_BT_GAP_DISC_RES_EVT:
            ESP_LOGI(SPP_TAG, "ESP_BT_GAP_DISC_RES_EVT");
            esp_log_buffer_hex(SPP_TAG, param->disc_res.bda, ESP_BD_ADDR_LEN);
            break;
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            ESP_LOGI(SPP_TAG, "ESP_BT_GAP_DISC_STATE_CHANGED_EVT");
            break;
        case ESP_BT_GAP_RMT_SRVCS_EVT:
            ESP_LOGI(SPP_TAG, "ESP_BT_GAP_RMT_SRVCS_EVT");
            ESP_LOGI(SPP_TAG, "%d", param->rmt_srvcs.num_uuids);
            break;
        case ESP_BT_GAP_RMT_SRVC_REC_EVT:
            ESP_LOGI(SPP_TAG, "ESP_BT_GAP_RMT_SRVC_REC_EVT");
            break;
        case ESP_BT_GAP_AUTH_CMPL_EVT:{
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(SPP_TAG, "authentication success: %s", param->auth_cmpl.device_name);
                esp_log_buffer_hex(SPP_TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
            } else {
                ESP_LOGE(SPP_TAG, "authentication failed, status:%d", param->auth_cmpl.stat);
            }
            break;
        }
        
        default:
            break;
    }
}
void app_main() {
    //GameCube Contoller reading init
    rmt_tx_init();
    xnes_init();
    xTaskCreatePinnedToCore(xnes_get_buttons, "gbuttons", 2048, NULL, 1, NULL, 1);
    //flash LED
    vTaskDelay(100);
    gpio_set_level(LED_GPIO, 0);
    vTaskDelay(100);
    gpio_set_level(LED_GPIO, 1);
    vTaskDelay(100);
    gpio_set_level(LED_GPIO, 0);
    vTaskDelay(100);
    gpio_set_level(LED_GPIO, 1);
    vTaskDelay(100);
    gpio_set_level(LED_GPIO, 0);
    const char* TAG = "app_main";
	esp_err_t ret;
    static esp_hidd_callbacks_t callbacks;
    static esp_hidd_app_param_t app_param;
    static esp_hidd_qos_param_t both_qos;

    xSemaphore = xSemaphoreCreateMutex();
    
    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = PIN_SEL;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    app_param.name = "BlueXNESMod";
    app_param.description = "BlueXNESMod Example";
    app_param.provider = "ESP32";
    app_param.subclass = 0x8;
    app_param.desc_list = hid_descriptor_gamecube;
    app_param.desc_list_len = hid_descriptor_gc_len;
    memset(&both_qos, 0, sizeof(esp_hidd_qos_param_t));

    callbacks.application_state_cb = application_cb;
    callbacks.connection_state_cb = connection_cb;
    callbacks.get_report_cb = get_report_cb;
    callbacks.set_report_cb = set_report_cb;
    callbacks.set_protocol_cb = set_protocol_cb;
    callbacks.intr_data_cb = intr_data_cb;
    callbacks.vc_unplug_cb = vc_unplug_cb;

	ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
    
    set_bt_address();
    
	ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

	esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_mem_release(ESP_BT_MODE_BLE);
    if ((ret = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "initialize controller failed: %s\n",  esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) {
        ESP_LOGE(TAG, "enable controller failed: %s\n",  esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bluedroid_init()) != ESP_OK) {
        ESP_LOGE(TAG, "initialize bluedroid failed: %s\n",  esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(TAG, "enable bluedroid failed: %s\n",  esp_err_to_name(ret));
        return;
    }
    esp_bt_gap_register_callback(esp_bt_gap_cb);
    ESP_LOGI(TAG, "setting hid parameters");
    esp_hid_device_register_app(&app_param, &both_qos, &both_qos);

	ESP_LOGI(TAG, "starting hid device");
	esp_hid_device_init(&callbacks);

    ESP_LOGI(TAG, "setting device name");
    esp_bt_dev_set_device_name("Pro Controller");

    ESP_LOGI(TAG, "setting to connectable, discoverable");
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    //start blinking
    xTaskCreate(startBlink, "blink_task", 1024, NULL, 1, &BlinkHandle);

    
}
