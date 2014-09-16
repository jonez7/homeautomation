#include <stdint.h>

#define MOTION_DETECTOR_PIN_STAIRS_DOWN     (11)
#define MOTION_DETECTOR_PIN_STAIRS_UP       (12)
#define MOTION_DETECTOR_PIN_STORAGE_ROOM    (10)
#define INTERNAL_LED_PIN                    (13)
#define OUTPUT_PIN_STAIRS                   (3)
#define OUTPUT_PIN_STORAGE                  (9)
#define CHECK_DELAY                         (250)
#define TIMEOUT_STAIRS                      (30) /*seconds*/
#define TIMEOUT_STORAGE                     (60) /*seconds*/
#define MAX_INPUT_PINS_PER_LIGHT            (2)
#define DELAY_TICKS(a)                      ((a) * 1000/CHECK_DELAY)

static uint8_t g_internalLedState = 0;

typedef struct Light_s {
    uint8_t    outPin;
    uint8_t    setPin[MAX_INPUT_PINS_PER_LIGHT];
    int16_t    brightness;
    uint16_t   timeout;
    uint16_t   timeOutSet;
    uint8_t    onSpeed;
    uint8_t    offSpeed;
    char     * name;
}Light_s;

Light_s g_LightStairs =  {OUTPUT_PIN_STAIRS,  
                          {MOTION_DETECTOR_PIN_STAIRS_UP, MOTION_DETECTOR_PIN_STAIRS_DOWN}, 
                          0,
                          0, 
                          DELAY_TICKS(TIMEOUT_STAIRS), 
                          20, 
                          50, 
                          "stairs"};
Light_s g_LightStorage = {OUTPUT_PIN_STORAGE, 
                          {MOTION_DETECTOR_PIN_STORAGE_ROOM, 0} , 
                          0, 
                          0,
                          DELAY_TICKS(TIMEOUT_STORAGE), 
                          5,  
                          5, 
                          "storage"} ;

Light_s * lights[] = {&g_LightStairs, &g_LightStorage};

void setup() {
    pinMode(OUTPUT_PIN_STAIRS,                OUTPUT);
    pinMode(OUTPUT_PIN_STORAGE,               OUTPUT);
    pinMode(INTERNAL_LED_PIN,                 OUTPUT);
    pinMode(MOTION_DETECTOR_PIN_STAIRS_UP,    INPUT);
    pinMode(MOTION_DETECTOR_PIN_STAIRS_DOWN,  INPUT);
    pinMode(MOTION_DETECTOR_PIN_STORAGE_ROOM, INPUT);
    Serial.begin(115200);
}

void ToggleLed(void) {
    digitalWrite(INTERNAL_LED_PIN, g_internalLedState);
    g_internalLedState += 1;
    g_internalLedState &= 0x1;
}

void LightOn(uint8_t const lightIndex) {
    Light_s * const light = lights[lightIndex];
    if(light->brightness == 0) {
        Serial.print(light->name);
        Serial.print(" ON ");
        for (light->brightness = 0; light->brightness < 255; light->brightness++) {
            analogWrite(light->outPin, light->brightness);
            delay(light->onSpeed);
            ToggleLed();
        }
        light->timeout = light->timeOutSet;
        Serial.print(light->timeout);
    }
}

void LightOffTimer(uint8_t const lightIndex) {
    Light_s * const light = lights[lightIndex];
    if(light->timeout>0) {
        light->timeout--;
    }
    if(light->timeout == 0) {
        for (light->brightness; light->brightness > 0; light->brightness--) {
            analogWrite(light->outPin, light->brightness);
            delay(light->offSpeed);
            ToggleLed();
        }
        light->brightness = 0;
        digitalWrite(light->outPin, LOW);
    }
}

void CheckInputs(void) {
    uint8_t i;
    uint8_t j;
    for(i=0; i<2; i++) {
        Light_s * light = lights[i];
        uint8_t pinStatus = 0;
        for(j=0; j<MAX_INPUT_PINS_PER_LIGHT; j++) {
            if (light->setPin[j] != 0) {
                pinStatus |= digitalRead(light->setPin[j]);
            }
            if (pinStatus) {
                LightOn(i);
            } else {
                LightOffTimer(i);
            }
        }
    }
}

void loop() {
    delay(CHECK_DELAY);
    CheckInputs();
    ToggleLed();
}
