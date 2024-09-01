/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"

#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

#include "FreeRTOS.h"
#include "task.h"
#include "bsp/board_api.h"
#include "tusb.h"

#include "usb_descriptors.h"
#include "log.h"


// Which core to run on if configNUMBER_OF_CORES==1
#ifndef RUN_FREE_RTOS_ON_CORE
#define RUN_FREE_RTOS_ON_CORE 0
#endif

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum  {
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

// Secondary button to confirm app is running and Action Button
#define SECONDARY_LED     10
#define ACTION_BUTTON     11

// Priorities of our threads - higher numbers are higher priority
#define MAIN_TASK_PRIORITY      ( tskIDLE_PRIORITY + 2UL )
#define BLINK_TASK_PRIORITY     ( tskIDLE_PRIORITY + 2UL )
#define JUST_ALIVE_TASK_PRIORITY     ( tskIDLE_PRIORITY + 2UL )
#define HID_TASK_PRIORITY       ( tskIDLE_PRIORITY + 1UL )

// Stack sizes of our threads in words (4 bytes)
#define MAIN_TASK_STACK_SIZE configMINIMAL_STACK_SIZE
#define BLINK_TASK_STACK_SIZE configMINIMAL_STACK_SIZE
#define JUST_ALIVE_TASK_STACK_SIZE configMINIMAL_STACK_SIZE
#define HID_TASK_STACK_SIZE configMINIMAL_STACK_SIZE

static void pico_set_led(bool led_on);
static void pico_init_led(void);
uint32_t button_read(void);
void led_blinking_task(__unused void *params);
void hid_task(__unused void *params);
void just_alive_task(__unused void *params);
//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+
void tud_mount_cb(void);
void tud_umount_cb(void);
void tud_suspend_cb(bool remote_wakeup_en);
void tud_resume_cb(void);
//--------------------------------------------------------------------+
// USB HID
//--------------------------------------------------------------------+
static void send_hid_report(uint8_t report_id, uint32_t btn);
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len);
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize);


void main_task(__unused void *params) {
    printf("Starting main_task!!!\n");
    // start the led blinking task
    xTaskCreate(led_blinking_task,
                "LedBlinkingTask",
                BLINK_TASK_STACK_SIZE,
                NULL,
                BLINK_TASK_PRIORITY,
                NULL);
    // start the led blinking task
    xTaskCreate(just_alive_task,
                "JustAliveTask",
                JUST_ALIVE_TASK_STACK_SIZE,
                NULL,
                JUST_ALIVE_TASK_PRIORITY,
                NULL);
    xTaskCreate(hid_task,
                "HIDTask",
                HID_TASK_STACK_SIZE,
                NULL,
                HID_TASK_PRIORITY,
                NULL);
    // USB Board init
    board_init();
    // // init device stack on configured roothub port
    tud_init(BOARD_TUD_RHPORT);

    if (board_init_after_tusb) {
      board_init_after_tusb();
    }
    
    while(true) {
        tud_task(); // tinyusb device task
    }
}

void vLaunch( void) {
    TaskHandle_t task;
    xTaskCreate(main_task, "MainThread", MAIN_TASK_STACK_SIZE, NULL, MAIN_TASK_PRIORITY, &task);

#if configUSE_CORE_AFFINITY && configNUMBER_OF_CORES > 1
    // we must bind the main task to one core (well at least while the init is called)
    vTaskCoreAffinitySet(task, 1);
#endif

    /* Start the tasks and timer running. */
    vTaskStartScheduler();
}

int main( void )
{
    stdio_init_all();

    /* Configure the hardware ready to run the demo. */
    const char *rtos_name;
#if (configNUMBER_OF_CORES > 1)
    rtos_name = "FreeRTOS SMP";
#else
    rtos_name = "FreeRTOS";
#endif

#if (configNUMBER_OF_CORES > 1)
    printf("Starting %s on both cores:\n", rtos_name);
    vLaunch();
#elif (RUN_FREE_RTOS_ON_CORE == 1 && configNUMBER_OF_CORES==1)
    printf("Starting %s on core 1:\n", rtos_name);
    multicore_launch_core1(vLaunch);
    while (true);
#else
    printf("Starting %s on core 0:\n", rtos_name);
    vLaunch();
#endif
    return 0;
}

// Turn led on or off
static void pico_set_led(bool led_on) {
#if defined PICO_DEFAULT_LED_PIN
    gpio_put(PICO_DEFAULT_LED_PIN, led_on);
#elif defined(CYW43_WL_GPIO_LED_PIN)
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
#endif
}

// Initialise led
static void pico_init_led(void) {
#if defined PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#elif defined(CYW43_WL_GPIO_LED_PIN)
    hard_assert(cyw43_arch_init() == PICO_OK);
    pico_set_led(false); // make sure cyw43 is started
#endif
    // Secondary LED
    gpio_init(SECONDARY_LED);
    gpio_set_dir(SECONDARY_LED, GPIO_OUT);
    gpio_init(ACTION_BUTTON);
    gpio_set_dir(ACTION_BUTTON, GPIO_IN);
    gpio_pull_up(ACTION_BUTTON);
}

uint32_t button_read(void){
    return !gpio_get(ACTION_BUTTON);
}

void led_blinking_task(__unused void *params) {
    printf("Starting led_blinking_task!!!\n");
    static bool led_state = false;
    pico_init_led();
    while (true) {
        pico_set_led(led_state);
        led_state = !led_state;
        vTaskDelay(blink_interval_ms);
    }
}

void just_alive_task(__unused void *params) {
    printf("Starting just_alive_task!!!\n");
    static bool led_state = false;
    pico_init_led();
    while (true) {
        gpio_put(SECONDARY_LED, led_state);
        led_state = !led_state;
        log_message("Test message with no variables.\n");
        log_message("Test message with one variable: %d\n", 42);
        log_message("just_alive_task - Led State=%d\n", led_state);
        vTaskDelay(500);
    }
}
// Every 10ms, we will sent 1 report for each HID profile (keyboard, mouse etc ..)
// tud_hid_report_complete_cb() is used to send the next report after previous one is complete
void hid_task(__unused void *params){
    const uint32_t interval_ms = 10;
    while(true){
        uint32_t const btn = button_read();

        // Remote wakeup
        if ( tud_suspended() && btn ){
          // Wake up host if we are in suspend mode
          // and REMOTE_WAKEUP feature is enabled by host
          tud_remote_wakeup();
        }
        else{
          // Send the 1st of report chain, the rest will be sent by tud_hid_report_complete_cb()
          send_hid_report(REPORT_ID_KEYBOARD, btn);
        }
        // Poll every 10ms
        vTaskDelay(interval_ms);
    }
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
  blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}

//--------------------------------------------------------------------+
// USB HID
//--------------------------------------------------------------------+

static void send_hid_report(uint8_t report_id, uint32_t btn)
{
  // skip if hid is not ready yet
  if ( !tud_hid_ready() ) return;

  switch(report_id)
  {
    case REPORT_ID_KEYBOARD:
    {
      // use to avoid send multiple consecutive zero report for keyboard
      static bool has_keyboard_key = false;

      if ( btn )
      {
        uint8_t keycode[6] = { 0 };
        keycode[0] = HID_KEY_A;

        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, keycode);
        has_keyboard_key = true;
      }else
      {
        // send empty key report if previously has key pressed
        if (has_keyboard_key) tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
        has_keyboard_key = false;
      }
    }
    break;

    case REPORT_ID_MOUSE:
    {
      int8_t const delta = 5;

      // no button, right + down, no scroll, no pan
      tud_hid_mouse_report(REPORT_ID_MOUSE, 0x00, delta, delta, 0, 0);
    }
    break;

    case REPORT_ID_CONSUMER_CONTROL:
    {
      // use to avoid send multiple consecutive zero report
      static bool has_consumer_key = false;

      if ( btn )
      {
        // volume down
        uint16_t volume_down = HID_USAGE_CONSUMER_VOLUME_DECREMENT;
        tud_hid_report(REPORT_ID_CONSUMER_CONTROL, &volume_down, 2);
        has_consumer_key = true;
      }else
      {
        // send empty key report (release key) if previously has key pressed
        uint16_t empty_key = 0;
        if (has_consumer_key) tud_hid_report(REPORT_ID_CONSUMER_CONTROL, &empty_key, 2);
        has_consumer_key = false;
      }
    }
    break;

    case REPORT_ID_GAMEPAD:
    {
      // use to avoid send multiple consecutive zero report for keyboard
      static bool has_gamepad_key = false;

      hid_gamepad_report_t report =
      {
        .x   = 0, .y = 0, .z = 0, .rz = 0, .rx = 0, .ry = 0,
        .hat = 0, .buttons = 0
      };

      if ( btn )
      {
        report.hat = GAMEPAD_HAT_UP;
        report.buttons = GAMEPAD_BUTTON_A;
        tud_hid_report(REPORT_ID_GAMEPAD, &report, sizeof(report));

        has_gamepad_key = true;
      }else
      {
        report.hat = GAMEPAD_HAT_CENTERED;
        report.buttons = 0;
        if (has_gamepad_key) tud_hid_report(REPORT_ID_GAMEPAD, &report, sizeof(report));
        has_gamepad_key = false;
      }
    }
    break;

    default: break;
  }
}

// Invoked when sent REPORT successfully to host
// Application can use this to send the next report
// Note: For composite reports, report[0] is report ID
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len)
{
  (void) instance;
  (void) len;
  (void) report;
  log_message("tud_hid_report_completed!!!\n", LOG_LEVEL_DEBUG);
  // Print the instance
  log_message("  Instance: %u\n", instance, LOG_LEVEL_DEBUG);

  // Print the length of the reportaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
  log_message("  Report Length: %u\n", LOG_LEVEL_DEBUG);

  // Print the report data (as hex values for better readability)
  log_message("  Report Data: \n", LOG_LEVEL_DEBUG);
  for (uint16_t i = 0; i < len; i++){
      log_message("    %02X\n", report[i], LOG_LEVEL_DEBUG);
  }
  log_message("\n");
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
  // TODO not Implemented
  (void) instance;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;

  return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
  (void) instance;

  if (report_type == HID_REPORT_TYPE_OUTPUT)
  {
    // Set keyboard LED e.g Capslock, Numlock etc...
    if (report_id == REPORT_ID_KEYBOARD)
    {
      // bufsize should be (at least) 1
      if ( bufsize < 1 ) return;

      uint8_t const kbd_leds = buffer[0];

      if (kbd_leds & KEYBOARD_LED_CAPSLOCK)
      {
        // Capslock On: disable blink, turn led on
        blink_interval_ms = 0;
        board_led_write(true);
      }else
      {
        // Caplocks Off: back to normal blink
        board_led_write(false);
        blink_interval_ms = BLINK_MOUNTED;
      }
    }
  }
}
