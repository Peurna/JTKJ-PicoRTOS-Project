#include <stdio.h>
#include <string.h>

#include <pico/stdlib.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>
#include "tkjhat/sdk.h"
#include "tusb.h"
#include <math.h>

// Exercise 4. Include the libraries necessaries to use the usb-serial-debug, and tinyusb
// Tehtävä 4 . Lisää usb-serial-debugin ja tinyusbin käyttämiseen tarvittavat kirjastot.



#define DEFAULT_STACK_SIZE 2048
#define CDC_ITF_TX      1
#define MORSE_MAX_LEN 64


// Tehtävä 3: Tilakoneen esittely Add missing states.
// Exercise 3: Definition of the state machine. Add missing states.
// Use the state names referenced in the tasks below
enum state { STATE_INPUT, STATE_TRANSLATE, STATE_DISPLAY};
enum state programState = STATE_INPUT;

char translatedChar = '\0';
char displayBuffer[2] = "";
char currentMorseSequence[MORSE_MAX_LEN] = "";
// Tehtävä 3: Valoisuuden globaali muuttuja
// Exercise 3: Global variable for ambient light
uint32_t ambientLight = 0;
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
    {"-....-", '-'}, {".--.-.", '@'},
    {NULL, '\0'}
};
/*static void btn_fxn(uint gpio, uint32_t eventMask) {

    toggle_red_led();
    
    // Tehtävä 1: Vaihda LEDin tila.
    //            Tarkista SDK, ja jos et löydä vastaavaa funktiota, sinun täytyy toteuttaa se itse.
    // Exercise 1: Toggle the LED. 
    //             Check the SDK and if you do not find a function you would need to implement it yourself. 
}*/


static void btn_1_dot(uint gpio, uint32_t eventMask) {
    char dot = '.';
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(inputQueue, &dot, &xHigherPriorityTaskWoken);
    toggle_red_led();
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void btn_2_dash(uint gpio, uint32_t eventMask) {
    char dash = '-';
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(inputQueue, &dash, &xHigherPriorityTaskWoken);
    toggle_red_led();
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void InputTask(void *arg) {
    (void)arg;
    char receivedChar;
    char buf[128];
    float ax, ay, az, gx, gy, gz, t;
    // Soitetaan pieni ääni merkiksi, että taski on käynnissä
    buzzer_play_tone(440, 100);

    for (;;) {
        if (xQueueReceive(inputQueue, &receivedChar, pdMS_TO_TICKS(50)) == pdPASS) {
            if (programState == STATE_INPUT) {
                buzzer_play_tone(880, 50);
                int len = strlen(currentMorseSequence);

                if (len < MORSE_MAX_LEN - 1) {
                    currentMorseSequence[len] = receivedChar;
                    currentMorseSequence[len + 1] = '\0';

                    snprintf(buf, sizeof(buf), "Sekvenssi: %s\n", currentMorseSequence);
                    printf("%s", buf);


                }
            }
        }

        // Read IMU periodically (every loop)
        if (ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &t) == 0) {
            // placeholder for future motion handling
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void TranslateTask(void *arg) {
    (void)arg;
    for (;;) {
        if (programState == STATE_TRANSLATE) {
            translatedChar = '\0';
            
            printf("Aloitetaan käännös sekvenssille: %s\n", currentMorseSequence);

            for (int i = 0; morse_map[i].morse != NULL; i++) {
                if (strcmp(currentMorseSequence, morse_map[i].morse) == 0) {
                    translatedChar = morse_map[i].character;
                    break;
                }
            }

            if (translatedChar != '\0') {
                printf("Käännetty merkiksi: %c\n", translatedChar);
                // Valmistellaan merkin näyttöbuffaus
                displayBuffer[0] = translatedChar;
                displayBuffer[1] = '\0';
            } else {
                printf("Virhe: Tuntematon morse-sekvenssi: %s\n", currentMorseSequence);
                // Asetetaan virhemerkki näytettäväksi
                translatedChar = '?';
                displayBuffer[0] = '?';
                displayBuffer[1] = '\0';
            }

            // Nollataan sekvenssi seuraavaa merkkiä varten
            currentMorseSequence[0] = '\0';

            // Siirry näytötilaan
            programState = STATE_DISPLAY;
        }
        
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void DisplayTask(void *arg) {
    (void)arg;
    for (;;) {
        if (programState == STATE_DISPLAY) {
            programState = STATE_INPUT; 
        }
        // TODO: implement display / sending of CSV (timestamp, luminance)
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
/*static void sensor_task(void *arg){
    (void)arg;

    init_veml6030();
    // Tehtävä 2: Alusta valoisuusanturi. Etsi SDK-dokumentaatiosta sopiva funktio.
    // Exercise 2: Init the light sensor. Find in the SDK documentation the adequate function.
   
    for(;;){
        ...
    }
}*/

/*static void print_task(void *arg){
    (void)arg;
    
    while(1){
        ...
    }
}*/


// Exercise 4: Uncomment the following line to activate the TinyUSB library.  
// Tehtävä 4:  Poista seuraavan rivin kommentointi aktivoidaksesi TinyUSB-kirjaston. 

/*
static void usbTask(void *arg) {
    (void)arg;
    while (1) {
        tud_task();              // With FreeRTOS wait for events
                                 // Do not add vTaskDelay. 
    }
}*/
static void orientation_task(void *arg) {
    (void)arg;

    if (init_ICM42670() != 0) {
        vTaskDelete(NULL);
    }

    ICM42670_start_with_default_values();

    float ax, ay, az, gx, gy, gz, t;

    const float SIDE_THRESHOLD_G = 0.8f; 

    for (;;) {
        ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &t);

        if (fabs(ax) > SIDE_THRESHOLD_G || fabs(ay) > SIDE_THRESHOLD_G) {

        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
int main() {

    stdio_init_all();
    sleep_ms(2000); 
    printf("--- Ohjelma käynnistyy ---\n");

    init_hat_sdk();
    sleep_ms(300); 

    init_sw1();
    init_sw2();
    init_red_led();
    init_buzzer();
    init_display();

    if (init_ICM42670() != 0) {
        printf("IMU-anturin alustus epäonnistui! PYSÄHDETTY.\n");
        while(1);
    }
    ICM42670_start_with_default_values();
    printf("Laitteisto alustettu.\n");

    gpio_set_irq_enabled_with_callback(BUTTON1, GPIO_IRQ_EDGE_FALL, true, btn_1_dot);
    gpio_set_irq_enabled_with_callback(BUTTON2, GPIO_IRQ_EDGE_FALL, true, btn_2_dash);

    inputQueue = xQueueCreate(10, sizeof(char));
    if (inputQueue == NULL) {
        printf("Jonon (queue) luonti epäonnistui! PYSÄHDETTY.\n");
        while(1);
    }
    printf("Jono luotu.\n");

    TaskHandle_t hInput, hTranslate, hDisplay;
    BaseType_t result;

    result = xTaskCreate(InputTask, "Input", DEFAULT_STACK_SIZE, NULL, 2, &hInput);
    if (result != pdPASS) {
        printf("InputTaskin luonti epäonnistui!\n");
    }

    result = xTaskCreate(TranslateTask, "Translate", DEFAULT_STACK_SIZE, NULL, 2, &hTranslate);
    if (result != pdPASS) {
        printf("TranslateTaskin luonti epäonnistui!\n");
    }

    result = xTaskCreate(DisplayTask, "Display", DEFAULT_STACK_SIZE, NULL, 2, &hDisplay);
    if (result != pdPASS) {
        printf("DisplayTaskin luonti epäonnistui!\n");
    }

    printf("Kaikki taskit luotu.\n");

    vTaskStartScheduler();

    return 0;
}

