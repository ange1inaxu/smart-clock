#include "extra.h"
#include <WiFi.h> //Connect to WiFi Network
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h> //Used in support of TFT Display
#include <string.h>  //used for some string handling and processing.
#include <mpu6050_esp32.h>
#include<math.h>

// state machine to alternate between with and without seconds
uint8_t state;
const uint8_t WITH_SEC = 0;
const uint8_t PUSH_W_SEC = 1;
const uint8_t NO_SEC = 2;
const uint8_t PUSH_NO_SEC = 3;

TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h

const int RESPONSE_TIMEOUT = 6000; //ms to wait for response from host
const uint16_t IN_BUFFER_SIZE = 1000; //size of buffer to hold HTTP request
const uint16_t OUT_BUFFER_SIZE = 1000; //size of buffer to hold HTTP response
char request_buffer[IN_BUFFER_SIZE]; //char array buffer to hold HTTP request
char response_buffer[OUT_BUFFER_SIZE]; //char array buffer to hold HTTP response
char response_buffer_copy[OUT_BUFFER_SIZE];

uint32_t last_time; //used for timing

const int BUTTON = 45; //pin connected to button 
const uint8_t BUTTON2 = 39; //pin connected to button

// WIFI connection
char network[] = "MIT GUEST";
char password[] = "";

uint8_t scanning = 0; //set to 1 if you'd like to scan for wifi networks (see below):
uint8_t channel = 1; //network channel on 2.4 GHz
byte bssid[] = {0x04, 0x95, 0xE6, 0xAE, 0xDB, 0x41}; //6 byte MAC address of AP you're targeting.

// state machine for sensing motion
int motion_state;
int ALWAYS_ON = 0;
int SENSE_MOTION = 1;

// state machine for whether the LCD Display is ON or OFF
int sleep_time;
int on_state;
int ON = 0;
int OFF = 1;

// variables for detecting motion
MPU6050 imu; //imu object called, appropriately, imu
float x, y, z; //variables for grabbing x,y,and z values
const float ZOOM = 9.81; //for display (converts readings into m/s^2)...used for visualizing only


void setup() {
  tft.init();
  tft.setRotation(2);
  tft.setTextSize(2);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  Serial.begin(115200); //begin serial

  // IMU setup
  Wire.begin();
  delay(50); //pause to make sure comms get set up
  if (imu.setupIMU(1)) {
    Serial.println("IMU Connected!");
  } else {
    Serial.println("IMU Not Connected :/");
    Serial.println("Restarting");
    ESP.restart(); // restart the ESP (proper way)
  }

  if (scanning){
    int n = WiFi.scanNetworks();
    Serial.println("scan done");
    if (n == 0) {
      Serial.println("no networks found");
    } else {
      Serial.print(n);
      Serial.println(" networks found");
      for (int i = 0; i < n; ++i) {
        Serial.printf("%d: %s, Ch:%d (%ddBm) %s ", i + 1, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "");
        uint8_t* cc = WiFi.BSSID(i);
        for (int k = 0; k < 6; k++) {
          Serial.print(*cc, HEX);
          if (k != 5) Serial.print(":");
          cc++;
        }
        Serial.println("");
      }
    }    
  }
  delay(100); //wait a bit (100 ms)

  //if using regular connection use line below:
  WiFi.begin(network, password);
  //if using channel/mac specification for crowded bands use the following:
  //WiFi.begin(network, password, channel, bssid);

  uint8_t count = 0; //count used for Wifi check times
  Serial.print("Attempting to connect to ");
  Serial.println(network);
  while (WiFi.status() != WL_CONNECTED && count < 6) { //can change this to more attempts
    delay(500);
    Serial.print(".");
    count++;
  }
  delay(2000);  //acceptable since it is in the setup function.
  if (WiFi.isConnected()) { //if we connected then print our IP, Mac, and SSID we're on
    Serial.println("CONNECTED!");
    Serial.printf("%d:%d:%d:%d (%s) (%s)\n", WiFi.localIP()[3], WiFi.localIP()[2],
                  WiFi.localIP()[1], WiFi.localIP()[0],
                  WiFi.macAddress().c_str() , WiFi.SSID().c_str());
    delay(500);
  } else { //if we failed to connect just Try again.
    Serial.println("Failed to Connect :/  Going to restart");
    Serial.println(WiFi.status());
    ESP.restart(); // restart the ESP (proper way)
  }
  randomSeed(analogRead(A0)); //"seed" random number generator


  pinMode(BUTTON, INPUT_PULLUP); //set input pin as an input!
  pinMode(BUTTON2, INPUT_PULLUP); //set input pin as an input!

  // initiate all state variables
  state = WITH_SEC;
  motion_state = SENSE_MOTION;
  on_state = OFF;
  sleep_time = millis();
}


// Slice original[start:end] and store in result
void slice(char* original, char* result, int start, int end)
{
  int i = 0;
  for (int j=start; j<end; j++){
    result[i] = original[j];
    i++;
  }
}


// Query for time from server
void get_time(){
  //formulate GET request...first line:
  sprintf(request_buffer, "GET http://iesc-s3.mit.edu/esp32test/currenttime HTTP/1.1\r\n");
  strcat(request_buffer, "Host: iesc-s3.mit.edu\r\n"); //add more to the end
  strcat(request_buffer, "\r\n"); //add blank line!
  //submit to function that performs GET.  It will return output using response_buffer char array
  char host[] = "iesc-s3.mit.edu";
  do_http_GET(host, request_buffer, response_buffer, OUT_BUFFER_SIZE, RESPONSE_TIMEOUT, true);
  // Serial.println(response_buffer); //print to serial monitor
}


// convert military_time to properly formatted_time with AM/PM notations
void format_time(char* military_time, char* formatted_time){
  char str_hour[3] = "";
  char str_rest[30] = "";

  slice(military_time, str_hour, 0, 2);  // slice for the hour
  slice(military_time, str_rest, 2, strlen(military_time)); // slice for the characters following the hour

  int int_hour = atoi(str_hour); // cast string into int
  if (int_hour >= 12){
    if (int_hour > 12) {
      int_hour = int_hour % 12;   // convert from military time to american time
    }
    sprintf(formatted_time, "%d%s PM", int_hour, str_rest);    // set result to be the string casting of int_hour
  } else {
    sprintf(formatted_time, "%d%s AM", int_hour, str_rest);    // set result to be the string casting of int_hour
  }
}


void loop() {
  // get IMU reading for acceleration
  imu.readAccelData(imu.accelCount);
  x = ZOOM * imu.accelCount[0] * imu.aRes;
  y = ZOOM * imu.accelCount[1] * imu.aRes;
  z = ZOOM * imu.accelCount[2] * imu.aRes;
  float acceleration = sqrt(x*x + y*y + z*z);

  int button2_reading = digitalRead(BUTTON2);

  if (motion_state == ALWAYS_ON){
    if (button2_reading == 0){ // pressed
      motion_state = SENSE_MOTION;      
    }
    LCD_display();

  } else if (motion_state == SENSE_MOTION){
    if (button2_reading == 0){ // pressed
      motion_state = ALWAYS_ON;
    }

    if (on_state == ON){
      LCD_display();

      if (acceleration <= 11){
        if (millis() - sleep_time >= 15000){
            on_state = OFF;
        }
      }
    } else if (on_state == OFF){
      sleep_time = millis();
      tft.fillScreen(TFT_BLACK); //black out TFT Screen
      if (acceleration > 11){
        on_state = ON;
      }
    }
  }
}


void LCD_display(){
  // only query server once per minute (60000 ms)
  if (millis() - last_time >= 60000){
    get_time();
    strcpy(response_buffer_copy, response_buffer);
    last_time = millis();
    display_time(digitalRead(BUTTON));
  }
  // otherwise, do computation since last query for current time
  else {
    time_without_lookup();
    display_time(digitalRead(BUTTON));
  }
}



char hourMinSec[20] = "";
char hourMin[20] = "";

void display_time(uint8_t input){
  if (state == WITH_SEC){
    if(input==0){ // pushed
      state = PUSH_W_SEC;
    }
    slice(response_buffer, hourMinSec, 11, 19);
    char american_time[30] = "";
    format_time(hourMinSec, american_time);
    tft.setCursor(0, 0, 1);
    tft.println(american_time);
  }

  else if (state == PUSH_W_SEC){
    if (input==1){ // unpushed
      state = NO_SEC;
      tft.fillScreen(TFT_BLACK); //black out TFT Screen
    }
  }

  else if (state == NO_SEC){
    slice(response_buffer, hourMin, 11, 16);
    char american_time[30] = "";
    format_time(hourMin, american_time);
    tft.setCursor(0, 0, 1);
      
    if(input==0){ // pushed
      state = PUSH_NO_SEC;
    }

    // don't display colon when seconds are even
    char str_sec[20] = "";
    slice(response_buffer, str_sec, 17, 19);
    int sec = atoi(str_sec);
    if (sec % 2 == 0){

      // find the index of the colon
      char* colon;
      int index;
      colon = strchr(american_time, ':');
      index = (int)(colon - american_time);

      // slice without the colon
      char result[20] = "";
      char result_beg[20] = "";
      char result_end[20] = "";
      slice(american_time, result_beg, 0, index);
      slice(american_time, result_end, index+1, strlen(american_time));
      sprintf(result, "%s %s", result_beg, result_end);
      tft.println(result);

    // display colon when seconds are odd
    } else {
      tft.println(american_time);
    }
  }
  
  else if (state == PUSH_NO_SEC){
      if (input==1){ // unpushed
        state = WITH_SEC;
        tft.fillScreen(TFT_BLACK); //black out TFT Screen
      }
  }
}

// Compute the current time, provided that you can only query periodically
void time_without_lookup(){
  char date[20] = "";
  char hour[3] = "";
  char min[3] = "";
  char sec[3] = "";
  char ms[4] = "";
  slice(response_buffer_copy, date, 0, 11);
  slice(response_buffer_copy, hour, 11, 13);
  slice(response_buffer_copy, min, 14, 16);
  slice(response_buffer_copy, sec, 17, 19);
  slice(response_buffer_copy, ms, 20, 23);

  int int_hour = atoi(hour);
  int int_min = atoi(min);
  int int_sec = atoi(sec);
  int int_ms = atoi(ms);

  int ms_diff = millis() - last_time;
  int new_ms = int_ms + ms_diff;

  // Update the hour, min, sec based on the change in ms
  if (new_ms >= 1000) {
    int_sec++;
    new_ms -= 1000;
  }
  if (int_sec >= 60) {
    int_min++;
    int_sec -= 60;
  }
  if (int_min >= 60){
    int_hour++;
    int_min -= 60;
  }

  sprintf(response_buffer, date);

  char temp[20] = "";
  if (int_hour < 10){
    sprintf(temp, "0%d:", int_hour);
  } else {
    sprintf(temp, "%d:", int_hour);
  }
  strcat(response_buffer, temp);

  if (int_min < 10){
    sprintf(temp, "0%d:", int_min);
  } else {
    sprintf(temp, "%d:", int_min);
  }
  strcat(response_buffer, temp);

  if (int_sec < 10){
    sprintf(temp, "0%d", int_sec);
  } else {
    sprintf(temp, "%d", int_sec);
  }
  strcat(response_buffer, temp);

}

/*----------------------------------
   char_append Function:
   Arguments:
      char* buff: pointer to character array which we will append a
      char c:
      uint16_t buff_size: size of buffer buff

   Return value:
      boolean: True if character appended, False if not appended (indicating buffer full)
*/
uint8_t char_append(char* buff, char c, uint16_t buff_size) {
  int len = strlen(buff);
  if (len > buff_size) return false;
  buff[len] = c;
  buff[len + 1] = '\0';
  return true;
}

/*----------------------------------
   do_http_GET Function:
   Arguments:
      char* host: null-terminated char-array containing host to connect to
      char* request: null-terminated char-arry containing properly formatted HTTP GET request
      char* response: char-array used as output for function to contain response
      uint16_t response_size: size of response buffer (in bytes)
      uint16_t response_timeout: duration we'll wait (in ms) for a response from server
      uint8_t serial: used for printing debug information to terminal (true prints, false doesn't)
   Return value:
      void (none)
*/
void do_http_GET(char* host, char* request, char* response, uint16_t response_size, uint16_t response_timeout, uint8_t serial) {
  WiFiClient client; //instantiate a client object
  if (client.connect(host, 80)) { //try to connect to host on port 80
    // if (serial) Serial.print(request);//Can do one-line if statements in C without curly braces
    client.print(request);
    memset(response, 0, response_size); //Null out (0 is the value of the null terminator '\0') entire buffer
    uint32_t count = millis();
    while (client.connected()) { //while we remain connected read out data coming back
      client.readBytesUntil('\n', response, response_size);
      // if (serial) Serial.println(response);
      if (strcmp(response, "\r") == 0) { //found a blank line! (end of response header)
        break;
      }
      memset(response, 0, response_size);
      if (millis() - count > response_timeout) break;
    }
    memset(response, 0, response_size);  //empty in prep to store body
    count = millis();
    while (client.available()) { //read out remaining text (body of response)
      char_append(response, client.read(), OUT_BUFFER_SIZE);
    }
    // if (serial) Serial.println(response);
    client.stop();
    /* if (serial) Serial.println("-----------");
  } else {
    if (serial) Serial.println("connection failed :/");
    if (serial) Serial.println("wait 0.5 sec..."); */
    client.stop();
  }
}
