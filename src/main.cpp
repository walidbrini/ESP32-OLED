
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
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#define UART2_TX_PIN 27
#define UART2_RX_PIN 26
#define INTERRUPT_PIN 22  // Define the button pin

# define DHTPIN 15
# define DHTTYPE DHT22
# define TAILLE_MAX  20
# define DEMO_DURATION 3000

// Set your Static IP address
IPAddress local_IP(192, 168, 137, 100);
// Set your Gateway IP address
IPAddress gateway(192, 168, 137, 1);
IPAddress subnet(255, 255, 0, 0);

DHT dht(DHTPIN,DHTTYPE); // Object declaraion

const char *ssid = "Walid";
const char *password = "12345678";

bool buttonState = false; // Initially button state is false

WebServer server(80);



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

unsigned long previousMillis = 0; // Variable to store the previous time when the screen was updated
const unsigned long displayDuration = 5000; // Duration for displaying the screen (5 seconds)
unsigned long current_time  ;
unsigned long start_time ; 
int demoMode = 0;
int counter = 1;
int table_pointer = 0 ; 
int x = 0 ; 
float temp,humidite ; 
u_int16_t taux_co2 ; 
bool interruptOccurred = false;  // Global flag to indicate an interrupt occurred
bool flag = true ; 

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

      current_time = millis() ; 

      if ( current_time - start_time> displayDuration){
        buttonState = true;  
      }

      if (buttonState){
        buttonState = false ; 
        interruptOccurred  = false ; 
        display.clear();
        draw_waiting();
        printf("Yes buttonn \n"); 
      }


      vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(250));
  
  }
  vTaskDelete(NULL); 
}

/*
  La periode de C02  
  taux de co2 periode plus grande */
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
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(500));
    }
}

void IRAM_ATTR interruptPersonne()
{ 
  interruptOccurred = true; 
  start_time = millis(); 
}


void handleRoot() {
  char msg[2500]; // Increased buffer size to accommodate additional data

  snprintf(msg, 2500,
           "<!DOCTYPE html>\
<html>\
<head>\
<meta http-equiv='refresh' content='4'/>\
<meta name='viewport' content='width=device-width, initial-scale=1'>\
<link rel='stylesheet' href='https://use.fontawesome.com/releases/v5.7.2/css/all.css' integrity='sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr' crossorigin='anonymous'>\
<title>RTOS Project Server</title>\
<style>\
html { font-family: Arial; display: inline-block; margin: 0px auto; text-align: center;}\
h2 { font-size: 3.0rem; }\
p { font-size: 3.0rem; }\
.units { font-size: 1.2rem; }\
.dht-labels{ font-size: 1.5rem; vertical-align:middle; padding-bottom: 15px;}\
.button { background-color: #4CAF50; border: none; color: white; padding: 20px 40px; text-align: center; text-decoration: none; font-size: 30px; margin: 4px 2px; cursor: pointer; border-radius: 12px; }\
.button:hover { background-color: #45a049; }\
</style>\
</head>\
<body>\
<h2>Projet FreeRTOS ESP32 Data Monitoring</h2>\
<p>\
<i class='fas fa-thermometer-half' style='color:#ca3517;'></i>\
<span class='dht-labels'>Temperature</span>\
<span>%.2f</span>\
<sup class='units'>&deg;C</sup>\
</p>\
<p>\
<i class='fas fa-tint' style='color:#00add6;'></i>\
<span class='dht-labels'>Humidity</span>\
<span>%.2f</span>\
<sup class='units'>&percnt;</sup>\
</p>\
<p>\
<span class='dht-labels'>CO2</span>\
<span>%u</span>\
<sup class='units'>ppm</sup>\
</p>\
<form action='/toggleButton'>\
  <button class='button' type='submit'>Toggle Button</button>\
</form>\
</body>\
</html>",
           temp, humidite, taux_co2, buttonState ? "ON" : "OFF"
          );
  server.send(200, "text/html", msg);
}


void handleToggleButton() {
  buttonState = !buttonState; // Toggle button state
  server.sendHeader("Location", "/");
  server.send(303);
}

void connect2Wifi(){

  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("STA Failed to configure");
  }
  // Connect to Wi-Fi network with SSID and password
  Serial.print("Connecting to ");
  Serial.print(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("Connected to WiFi");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);

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
  mutex = xSemaphoreCreateMutex(); 
  
  xBinarySemaphore = xSemaphoreCreateBinary();

  // Create Tasks 
  draw_waiting(); 

  pinMode(INTERRUPT_PIN, INPUT_PULLDOWN);  // Set button pin as input with pull-up resistor
  attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), interruptPersonne, RISING); // Attach interrupt to button pin, trigger on falling edge

  xTaskCreatePinnedToCore( vProducteurTemperature, "ProducteurTemp", 10000, NULL, 1, NULL , 0 ); 
  xTaskCreatePinnedToCore( vProducteurHumidite, "ProducteurHumidite", 10000, NULL, 1, NULL , 0 );  
  xTaskCreatePinnedToCore( vProducteurCo2, "ProducteurCo2", 10000, NULL, 1, NULL , 0 );  
  xTaskCreatePinnedToCore( vConsomateur, "Consomateur", 10000, NULL, 1 ,NULL ,  0 );

  
  connect2Wifi();
  if (MDNS.begin("esp32")) {
    Serial.println("MDNS responder started");
  }
  server.on("/", handleRoot); // Set up the root page handler
  server.on("/toggleButton", handleToggleButton); // Set up the toggle button handler
  server.begin();
  Serial.println("HTTP server started");

}

void loop() {
  server.handleClient();
  delay(2);
}
