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
// We are aiming for grade 1 and level 1
// Point sharing: 2 points to Eemil, 2 points to Santtu and 2 points for Joonas.
extern void write_text_xy(int16_t x0, int16_t y0, const char *text);

enum state { STATE_INPUT, STATE_TRANSLATE, STATE_DISPLAY};
enum state programState = STATE_INPUT;

char translatedMessage[MESSAGE_MAX_LEN] = ""; // Array where the current translated message is stored
char currentMorseSequence[MORSE_MAX_LEN] = ""; // Array where the current inputted morse sequence is stored
QueueHandle_t inputQueue;

typedef struct { //Defining the morse_map constructor
    const char *morse;
    char character;
} MorseMapEntry;

const MorseMapEntry morse_map[] = { // Global list which operates as a databank for corresponding characters for each morse sequence
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

    if (gpio == BUTTON1) { // Checks which GPIO pin was triggered and then assigness the character
        button_char = '.';
    } else if(gpio == BUTTON2) {
        button_char = '-';
    }
    xQueueSendFromISR(inputQueue, &button_char, NULL); // Send the character to the queue from the ISR
    toggle_red_led(); // Red led to show the button has been pressed
 }

static void InputTask(void *arg) {
(void)arg;
    char receivedChar; // Local variables that are needed for the function
    char buf[128];
    float ax, ay, az, gx, gy, gz, t;
    const float TILT_THRESHOLD = 0.5f;
    const float RESET_THRESHOLD = 0.5f;
    bool motion_action_taken = false;
    buzzer_play_tone(440, 100);

    for (;;) { 

        if (xQueueReceive(inputQueue, &receivedChar, pdMS_TO_TICKS(50)) == pdPASS) { // Waits for a character from the inputQueue
            if (programState == STATE_INPUT) { // Only when in correct state will the next line of code run
                buzzer_play_tone(880, 50);
                int len = strlen(currentMorseSequence);

                if (len < MORSE_MAX_LEN - 1) { 
                    currentMorseSequence[len] = receivedChar; // Assigns the character to correct position in line
                    currentMorseSequence[len + 1] = '\0'; // After that we need to assign \0 again because receivedChar took the spot

                    snprintf(buf, sizeof(buf), "Sekvenssi: %s\n", currentMorseSequence); // Printing sequence for debugging
                    printf("%s", buf);
                } 
            }
        }

        if (ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &t) == 0) { //tests that the values are zero to continue
            
            if (fabs(ax) < RESET_THRESHOLD && fabs(ay) < RESET_THRESHOLD) { //sets the condition when the device is flat and sets motion_action_taken to false
                motion_action_taken = false;
            }

            if (!motion_action_taken && programState == STATE_INPUT) { //if the motion_action_taken has not been taken AND program state is STATE_INPUT, this allows the code to progess to check which direction is taken nexty

                if (ax > TILT_THRESHOLD) { //when turning the device right (going over the threshold)
                    int len = strlen(currentMorseSequence); //count how many characters are in the currentMorseSequence
                    
                    programState = STATE_DISPLAY; //sets to state to STATE_DISPLAY
                    motion_action_taken = true; //and motion_action_taken to true, so it can't detect the motion more than once and requires it to be returned to flat
                    
                    buzzer_play_tone(1000, 80);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    buzzer_play_tone(1000, 80); //beep

                }


                else if (ax < -TILT_THRESHOLD) { //when turning the device left (going over the threshold but negative = opposite side)
                    int len = strlen(currentMorseSequence);
                    
                    if (len < MORSE_MAX_LEN - 3) {                   
                        programState = STATE_TRANSLATE; //sets the state to STATE_TRANSLATE to activate a function in TransLateTask
                        
                        buzzer_play_tone(700, 80); 
                        vTaskDelay(pdMS_TO_TICKS(100));
                        buzzer_play_tone(700, 80); //beep
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
    char singleChar = '\0';

    for (;;) { // TranslateTask is used only when the programState is on STATE_TRANSLATE
        if (programState == STATE_TRANSLATE) {
            singleChar = '\0'; // Temporary variable for the translation

            if (strlen(currentMorseSequence) == 0) { 
                singleChar = ' ';  // First check if the sequence is empty, if it is then add a "space"
            } else {
                for (int i = 0; morse_map[i].morse != NULL; i++) { //If the sequence is not empty, use stringcompare to find the matching morse from morse_map
                    if (strcmp(currentMorseSequence, morse_map[i].morse) == 0) { 
                        singleChar = morse_map[i].character; //Then set the corresponding character for the morse sequence as the value of singleChar
                        break;
                    }
                }
                if (singleChar == '\0') singleChar = '?'; // If the inputted morse is not on the list, throw an error and give singleChar the value of ?
            }

            int msg_len = strlen(translatedMessage);
            if (msg_len < MESSAGE_MAX_LEN - 1) { //Ensure that the current translatedMessage doesnt excede the max size
                translatedMessage[msg_len] = singleChar;
                translatedMessage[msg_len + 1] = '\0'; //Add the current translated singleChar as the rightmost character to the translatedMessage array
                printf("Käännetty: %s\n", translatedMessage);
            }

            currentMorseSequence[0] = '\0'; // Clear the morse sequence for the next input
            programState = STATE_INPUT; // Switch the program state back to STATE_INPUT
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void DisplayTask(void *arg) {
    (void)arg;
    for (;;) { //loops forever
        if (programState == STATE_DISPLAY) { //if gotten STATE_DISPLAY state from ICM sensor, this part can be progressed
            write_text_xy(0, 0, translatedMessage); //sets the coordinates of text to (0, 0), (x, y) on display (left uppper corner)
            vTaskDelay(pdMS_TO_TICKS(100));
            translatedMessage[0] = '\0'; //resetting values
            currentMorseSequence[0] = '\0';
            programState = STATE_INPUT; //return back to STATE_INPUT so another translate can be done
            vTaskDelay(pdMS_TO_TICKS(5000)); //5 second delay to see the printed text on display before it is cleared
            clear_display(); 
        }

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
    init_display();

    clear_display(); // Clearing the display to ensure clean start state

    ICM42670_start_with_default_values();

    inputQueue = xQueueCreate(10, sizeof(char)); // Createng a FreeRTOS queue to hold characters
    if (inputQueue == NULL) {
        printf("Jonon luonti epäonnistui!\n");
        while(1);
    }
    // Priorities: InputTask is higher (2) to ensure responsive button/sensor handling
    xTaskCreate(InputTask, "Input", 2048, NULL, 2, NULL); 
    xTaskCreate(TranslateTask, "Translate", 2048, NULL, 1, NULL);
    xTaskCreate(DisplayTask, "Display", 2048, NULL, 1, NULL);
    // Setup GPIO interrupts for buttons
    gpio_set_irq_enabled_with_callback(BUTTON1, GPIO_IRQ_EDGE_FALL, true, Button); // Register Button as the callback function for falling edge events
    gpio_set_irq_enabled(BUTTON2, GPIO_IRQ_EDGE_FALL, true); 

    vTaskStartScheduler();

    return 0;
}

// Developers: Joonas Hannula, Eemil Hyyppä, Santtu Mörsky

