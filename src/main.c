#include <stdio.h>
#include <string.h>
#include <math.h>
#include <pico/stdlib.h>
#include "tusb.h"
#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>
#include "tkjhat/sdk.h"

#define DEFAULT_STACK_SIZE 2048
#define CDC_ITF_TX      1
#define MORSE_MAX_LEN 64
#define MESSAGE_MAX_LEN 128

extern void clear_display();
extern void write_text_xy(int16_t x0, int16_t y0, const char *text);

enum state { STATE_INPUT, STATE_TRANSLATE, STATE_DISPLAY};
enum state programState = STATE_INPUT;

char translatedMessage[MESSAGE_MAX_LEN] = "";
char currentMorseSequence[MORSE_MAX_LEN] = "";
QueueHandle_t inputQueue;

typedef struct {
    const char *morse;
    char character;
} MorseMapEntry;

const MorseMapEntry morse_map[] = {
    {".-", 'A'}, {"-...", 'B'}, {"-.-.", 'C'}, {"-..", 'D'},
    {".", 'E'}, {"..-.", 'F'}, {"--.", 'G'}, {"....", 'H'},
    {"..", 'I'}, {".---", 'J'}, {"-.-", 'K'}, {".-..", 'L'},
    {"--", 'M'}, {"-.", 'N'}, {"---", 'O'}, {".--.", 'P'},
    {"--.-", 'Q'}, {".-.", 'R'}, {"...", 'S'}, {"-", 'T'},
    {"..-", 'U'}, {"...-", 'V'}, {".--", 'W'}, {"-..-", 'X'},
    {"-.--", 'Y'}, {"--..", 'Z'},
    {".----", '1'}, {"..---", '2'}, {"...--", '3'}, {"....-", '4'},
    {".....", '5'}, {"-....", '6'}, {"--...", '7'}, {"---..", '8'},
    {"----.", '9'}, {"-----", '0'},
    {".-.-.-", '.'}, {"--..--", ','}, {"..--..", '?'}, {"-.-.--", '!'},
    {"-....-", '-'}, {".--.-.", '@'}, {NULL, '\0'}
};

 static void Button(uint gpio, uint32_t events) {
    char button_char = 0;

    if (gpio == BUTTON1) {
        button_char = '.';
    } else if(gpio == BUTTON2) {
        button_char = '-';
    }
    xQueueSendFromISR(inputQueue, &button_char, NULL);
    toggle_red_led();
 }

static void InputTask(void *arg) {
(void)arg;
    char receivedChar;
    float ax, ay, az, gx, gy, gz, t;
    const float TILT_THRESHOLD = 0.7f;
    const float RESET_THRESHOLD = 0.5f;
    bool motion_action_taken = false;
    buzzer_play_tone(440, 100);

    for (;;) { 

        if (xQueueReceive(inputQueue, &receivedChar, pdMS_TO_TICKS(50)) == pdPASS) {
            if (programState == STATE_INPUT) {
                buzzer_play_tone(880, 50);
                int len = strlen(currentMorseSequence);

                if (len < MORSE_MAX_LEN - 1) {
                    currentMorseSequence[len] = receivedChar;
                    currentMorseSequence[len + 1] = '\0';
                } 
            }
        }

        if (ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &t) == 0) {
            
            if (fabs(ax) < RESET_THRESHOLD && fabs(ay) < RESET_THRESHOLD) {
                motion_action_taken = false;
            }

            if (!motion_action_taken && programState == STATE_INPUT) {

                if (ax > TILT_THRESHOLD) {
                    int len = strlen(currentMorseSequence);

                    if (len < MORSE_MAX_LEN - 1) {
                        currentMorseSequence[len] = '_';
                        currentMorseSequence[len + 1] = '\0';                        
                        buzzer_play_tone(600, 150); 
                    }
                    motion_action_taken = true;
                }


                else if (ax < -TILT_THRESHOLD) {
                    int len = strlen(currentMorseSequence);
                    
                    if (len < MORSE_MAX_LEN - 3) {
                        strcat(currentMorseSequence, "___");                       
                        programState = STATE_TRANSLATE;
                        
                        buzzer_play_tone(700, 80); 
                        vTaskDelay(pdMS_TO_TICKS(100));
                        buzzer_play_tone(700, 80);
                    }
                    motion_action_taken = true;
                }
            }
            
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
static void TranslateTask(void *arg) {
    (void)arg;
    char singleChar;

    for (;;) {
        if (programState == STATE_TRANSLATE) {
            singleChar = '\0';
            
            for (int i = 0; morse_map[i].morse != NULL; i++) {
                if (strcmp(currentMorseSequence, morse_map[i].morse) == 0) {
                    singleChar = morse_map[i].character;
                    break;
                }
            }

            if (singleChar != '\0') {
                printf("Käännetty merkiksi: %c\n", singleChar);
            } else {
                printf("Virhe: Tuntematon morse-sekvenssi: %s\n", currentMorseSequence);
                singleChar = '?';
            }
            int msg_len = strlen(translatedMessage);
            if (msg_len < MESSAGE_MAX_LEN - 1) {
                translatedMessage[msg_len] = singleChar;
                translatedMessage[msg_len + 1] = '\0';
                printf("Käännetty viesti: %s\n", translatedMessage);
            } else {
                printf("Huom Viestipuskuri täynnä\n");
            }

            currentMorseSequence[0] = '\0';
            programState = STATE_DISPLAY;
        }
        
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void DisplayTask(void *arg) {
    (void)arg;
    for (;;) {
        if (programState == STATE_DISPLAY) {
            clear_display();
            write_text_xy(0, 0, translatedMessage);
            vTaskDelay(pdMS_TO_TICKS(10000))
            translatedMessage[0] = '\0';
            currentMorseSequence[0] = '\0';
            programState = STATE_INPUT; 
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

int main() {
    stdio_init_all();
    sleep_ms(2000); 

    init_hat_sdk();
    sleep_ms(300); 

    init_sw1();
    init_sw2();
    init_red_led();
    init_buzzer();
    extern void init_display();
    init_display();

    ICM42670_start_with_default_values();

    gpio_set_irq_enabled_with_callback(BUTTON1, GPIO_IRQ_EDGE_FALL, true, Button);
    gpio_set_irq_enabled(BUTTON2, GPIO_IRQ_EDGE_FALL, true);

    vTaskStartScheduler();

    return 0;
}
