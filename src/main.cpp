
#include <Wire.h>               // Only needed for Arduino 1.6.5 and earlier
#include "SSD1306Wire.h"        // legacy: #include "SSD1306.h"
#include "images.h"
#include <Arduino.h>
#include <Adafruit_Sensor.h>
#include<DHT.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#define UART2_TX_PIN 27
#define UART2_RX_PIN 26
#define INTERRUPT_PIN 22  // Define the button pin

# define DHTPIN 15
# define DHTTYPE DHT22
# define TAILLE_MAX  20
# define DEMO_DURATION 3000

DHT dht(DHTPIN,DHTTYPE); // Object declaraion

typedef struct {
  float mesure;
  char type_capteur;
} mesure_t;

mesure_t tab_mesure[TAILLE_MAX] ; 
SemaphoreHandle_t s1 = NULL; 
SemaphoreHandle_t s2 = NULL; 
SemaphoreHandle_t mutex = NULL ; 
SemaphoreHandle_t xBinarySemaphore = NULL;

// Initialize the OLED display using Arduino Wire:
SSD1306Wire display(0x3c, 5, 4);   // ADDRESS, SDA, SCL  -  SDA and SCL usually populate automatically based on your board's pins_arduino.h e.g. https://github.com/esp8266/Arduino/blob/master/variants/nodemcu/pins_arduino.h

typedef void (*Screen)(void);

int demoMode = 0;
int counter = 1;
int table_pointer = 0 ; 
int x = 0 ; 
float temp,humidite ; 
u_int16_t taux_co2 ; 
bool interruptOccurred = false;  // Global flag to indicate an interrupt occurred

void drawFontFaceDemo() {
  // Font Demo1
  // create more fonts at http://oleddisplay.squix.ch/
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "Hello world");
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 10, "Hello world");
  display.setFont(ArialMT_Plain_24);
  display.drawString(0, 26, "Hello world");
}

void drawTextAlignmentDemo() {
  // Text alignment demo
  display.setFont(ArialMT_Plain_10);



  // The coordinates define the left starting point of the text
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 10, "Left aligned (0,10)");

  // The coordinates define the center of the text
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 22, "Center aligned (64,22)");

  // The coordinates define the right end of the text
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  display.drawString(128, 33, "Right aligned (128,33)");
}

void draw_waiting() {
  display.clear(); 
  // Text alignment demo
  display.setFont(ArialMT_Plain_10);
  // The coordinates define the center of the text
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 22, "Waiting for Person");
  display.display();
}

void drawProgressBarDemo() {
  int progress = (counter / 5) % 100;
  // draw the progress bar
  display.drawProgressBar(0, 32, 120, 10, progress);

  // draw the percentage as String
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 15, String(progress) + "%");
}

void update_screen() {
  // See http://blog.squix.org/2015/05/esp8266-nodemcu-how-to-create-xbm.html
  // on how to create XBM files
  display.drawXbm(0, 0, temp_Logo_width, temp_Logo_height, sensor_screen_bits2);
  
  // Convert temperature and humidity to strings
  char tempStr[8]; // Buffer to hold temperature string
  char humidityStr[8]; // Buffer to hold humidity string
  char co2Str[8] ;

  sprintf(co2Str, "%d", taux_co2);

  char ppm[4] = "ppm"; 

  dtostrf(temp, 4, 1, tempStr); // Convert float to string with 1 decimal place
  dtostrf(humidite, 4, 1, humidityStr); // Convert float to string with 1 decimal place
  strcat(tempStr, "°C");
  strcat(humidityStr, "%");
  display.setFont(ArialMT_Plain_16);

  // Display temperature
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(42, 8, tempStr);

  // Display humidity
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(42, 40, humidityStr);

  // Display Co2
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(100, 30, co2Str);
  display.setFont(ArialMT_Plain_10);
  display.drawString(100, 44, ppm);

  display.display();
}

void vProducteurTemperature(void *pvParameters)
{
    const char *pcTaskName = "ProducteurTemp";
    int valueToSend;
    BaseType_t status;
    UBaseType_t uxPriority;
    valueToSend = (int)pvParameters;
    uxPriority = uxTaskPriorityGet(NULL);
    int val;
    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();

    float temp;
    mesure_t mesure_container_temp;

    for (;;)
    {
        // Read Temperature
        temp = dht.readTemperature();
        mesure_container_temp.mesure = temp;
        mesure_container_temp.type_capteur = 'T';

        // Publish to buffer
        xSemaphoreTake(s1, portMAX_DELAY);
        xSemaphoreTake(mutex, portMAX_DELAY);
        tab_mesure[table_pointer] = mesure_container_temp;
        table_pointer = (table_pointer + 1) % TAILLE_MAX;
        xSemaphoreGive(mutex);
        xSemaphoreGive(s2);
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(250));
    }
}

void vProducteurHumidite(void *pvParameters)
{
    const char *pcTaskName = "ProducteurHumid";
    int valueToSend;
    BaseType_t status;
    UBaseType_t uxPriority;
    valueToSend = (int)pvParameters;
    uxPriority = uxTaskPriorityGet(NULL);
    int val;
    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();

    float humidity;
    mesure_t mesure_container_humidity;

    for (;;)
    {
        // Read Humidity
        humidity = dht.readHumidity();
        mesure_container_humidity.mesure = humidity;
        mesure_container_humidity.type_capteur = 'H';

        // Publish to buffer
        xSemaphoreTake(s1, portMAX_DELAY);
        xSemaphoreTake(mutex, portMAX_DELAY);
        tab_mesure[table_pointer] = mesure_container_humidity;
        table_pointer = (table_pointer + 1) % TAILLE_MAX;
        xSemaphoreGive(mutex);
        xSemaphoreGive(s2);
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(250));
    }
}

void vConsomateur(void *pvParameters)
{
  const char *pcTaskName = "Consomateur";
  UBaseType_t uxPriority;
  uxPriority = uxTaskPriorityGet(NULL);
  int i = 0;
  mesure_t current_mesure ;
  TickType_t xLastWakeTime;
  xLastWakeTime = xTaskGetTickCount();
  for (;;)
  {   
      
      xSemaphoreTake(s2, portMAX_DELAY); 
      // Consume value
      current_mesure = tab_mesure[i] ; 
      i = (i + 1) % TAILLE_MAX;
      xSemaphoreGive(s1); 
      if (current_mesure.type_capteur == 'H') {
        humidite = current_mesure.mesure ; 
        printf("Le consomateur a consomé %f de type Humidité \n ",humidite);
      }
      else if (current_mesure.type_capteur == 'T'){
        temp = current_mesure.mesure ;
        printf("Le consomateur a consomé %f de type temp \n ",temp);

      }
      else if (current_mesure.type_capteur == 'C'){
        taux_co2 = current_mesure.mesure ; 
        printf("Le consomateur a consomé %d de type C02 \n ",taux_co2);
      }
      if (interruptOccurred){
      display.clear();
      update_screen();
      }
      
      vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(250));
  
  }
  vTaskDelete(NULL); 
}

void vProducteurCo2(void * pvParameters){
    const char *pcTaskName = "ProducteurCo2";
    int valueToSend;
    BaseType_t status;
    UBaseType_t uxPriority;
    
    // Convert pvParameters to int
    valueToSend = (int)pvParameters;
    
    uxPriority = uxTaskPriorityGet(NULL);
    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();
    mesure_t mesure_container_co2 ;
    mesure_container_co2.type_capteur = 'C';

    for (;;) {
        if (Serial2.available() >= 0) {
            u_int16_t receivedData;  // Temporary variable to hold received data
            Serial2.readBytes((char*)&receivedData, sizeof(receivedData));  // Read bytes into temporary variable
            taux_co2 = receivedData;  // Assign received data to taux_co2
            //Serial.print("Received Co2: ");
            //Serial.println(taux_co2);
            mesure_container_co2.mesure =  float(taux_co2) ;
        }
        // Publish to buffer
        xSemaphoreTake(s1, portMAX_DELAY);
        xSemaphoreTake(mutex, portMAX_DELAY);
        tab_mesure[table_pointer] = mesure_container_co2;
        table_pointer = (table_pointer + 1) % TAILLE_MAX;
        xSemaphoreGive(mutex);
        xSemaphoreGive(s2);
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(250));
    }
}



void IRAM_ATTR interruptPersonne()
{ 
  interruptOccurred = true; 
}


Screen screens[] = {draw_waiting, update_screen};
int number_screens = (sizeof(screens) / sizeof(Screen));
long timeSinceLastModeSwitch = 0;

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println();

  Serial2.begin(9600, SERIAL_8N1, UART2_RX_PIN, UART2_TX_PIN);

  // Initialising the UI will init the display too.
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);

  // Init the Temp Sensor 
  dht.begin();  // Iniialize sensor
  //delay(2000); // to get accurate values

  s1 = xSemaphoreCreateCounting( TAILLE_MAX, TAILLE_MAX );
  s2 = xSemaphoreCreateCounting( TAILLE_MAX, 0 );
  xBinarySemaphore = xSemaphoreCreateBinary();
  mutex = xSemaphoreCreateMutex(); 
  
  // Create Tasks 
  draw_waiting(); 

  pinMode(INTERRUPT_PIN, INPUT_PULLDOWN);  // Set button pin as input with pull-up resistor
  attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), interruptPersonne, RISING); // Attach interrupt to button pin, trigger on falling edge

  xTaskCreatePinnedToCore( vProducteurTemperature, "ProducteurTemp", 10000, NULL, 1, NULL , 0 ); 
  xTaskCreatePinnedToCore( vProducteurHumidite, "ProducteurHumidite", 10000, NULL, 1, NULL , 0 );  
  xTaskCreatePinnedToCore( vProducteurCo2, "ProducteurCo2", 10000, NULL, 1, NULL , 0 );  
  xTaskCreatePinnedToCore( vConsomateur, "Consomateur", 10000, NULL, 1 ,NULL ,  0 );
    
}

void loop() {
}
