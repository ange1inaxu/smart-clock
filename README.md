# Demonstration Video
[demonstration video](https://youtu.be/wpR9EHQ8SB8)

# Two Display Modes (with and without seconds)

To implement a state machine that alternated between displaying the time with and without seconds, I implemented 4 states with the following variables:
```cpp
uint8_t state;
const uint8_t WITH_SEC = 0;
const uint8_t PUSH_W_SEC = 1;
const uint8_t NO_SEC = 2;
const uint8_t PUSH_NO_SEC = 3;
```

To facilitate the display_time function, I created two helper functions:

1) slice
```cpp
void slice(char* original, char* result, int start, int end)
{
  int i = 0;
  for (int j=start; j<end; j++){
    result[i] = original[j];
    i++;
  }
}
```

2) format_time
```cpp
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
```

These two functions introduce abstraction into our code and compartamentalizes repeated functions, allowing us to slice response_buffer into its constituent hours, minutes, seconds, and millisec components. format_time enables us to convert our response_buffer in Military Time to American Time.

In the display_function, if `state == WITH_SEC`, the TFT LCD screen would display the properly formatted American Time (with seconds), along with AM or PM, depending on if the hour in military time was >= 12. If the button connected to I/O 45 is pressed, the state would be changed to PUSH_W_SEC. In the `state == PUSH_W_SEC` conditional, if button 45 is unpushed, the state is switched to NO_SEC. In this conditional, the two previously mentioned helper functions are used to display the properly formatted American Time (without seconds) on the LCD SCREEN. In this state, to ensure that the colon flashes every second, I found if the seconds were even or odd. If the seconds were even, I would splice out the colon. Otherwise, I would display the time with the colon. Finally, if the button is pushed, the state is turned into PUSH_NO_SEC. In this state, if the button is released, the state is cycled back to WITH_SEC.


# Periodic Querying
To ensure that the time is only queried from the server once per minute, I set `last_time = millis()` each time after calling `get_time()` and getting response_buffer from the server.

```cpp
void LCD_display(){
  // only query once per minute
  if (millis() - last_time >= 60000){
    get_time();
    strcpy(response_buffer_copy, response_buffer);
    last_time = millis();
    display_time(digitalRead(BUTTON));
  }
  else {
    time_without_lookup();
    display_time(digitalRead(BUTTON));
  }
}
```

Only when `millis() - last_time >= 60000` ms or 1 minute, are we allowed to query the server again. Otherwise, we would compute the accurate time using the helper function `time_without_lookup()`. This function works by slicing the response_buffer into `date, hour, min, sec,` and `ms`. By finding `ms_diff = millis() - last_time`, we can find the change in ms from the last time we queried from the server. Using arithmetic, we can calculate the updated time, reconstruct response_buffer, and display the result on our LCD screen.

# Motion sensing
I implemented a state machine that toggled between ALWAYS_ON and SENSE_MOTION depending on the button 39 reading and I used the IMU to sense motion by calculating the magnitude of accleration. If the motion_state was ALWAYS_ON and button 39 was pressed, the motion_state was toggled to SENSE_MOTION. Another state machine was created to detect if the LCD Screen's on_state is ON or OFF. If the on_state is ON, then the time would be displayed using the `LCD_display()` helper function. If the acceleration reading from the IMU was <= 11 (ie. at rest) for over 15000 ms since the last time its on_state was OFF (indicated by `sleep_time`), then the on_state would be toggled to OFF. If the on_state is OFF, the TFT screen is blacked out. If any acceleration is detected > 11, the on_state is toggled back on. And this state machine and process repeats.

```cpp
if (motion_state == SENSE_MOTION){
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
```