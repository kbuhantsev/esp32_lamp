#include <Arduino.h>
#include <WiFi.h>

#include <microDS3231.h>
#include <OneButton.h>
#include <GTimer.h>
#include <GyverNTP.h>

#define LEDC_TIMER_12_BIT 12
#define LEDC_BASE_FREQ 5000
#define LED_PIN 0
#define BUTTON_PIN 1
#define LED_IND_PIN 2
#define LEDC_TARGET_DUTY 2300  //2200

const char* ssid = "TP-Link_C5_Pro";
const char* password = "AsdQwe12345";

MicroDS3231 rtc;

OneButton button = OneButton(
  BUTTON_PIN,
  true);

GTimerCb<millis> serial_timer;
GTimerCb<millis> ntp_timer;

int brights[8] = { 10, 50, 80, 100, 80, 50, 10, 0 };
int fade_times[2] = { 15, 3 };
int current_duty = 0;
int current_mode = 0;
bool auto_mode = true;

String inputString = "";      // a String to hold incoming data.
bool stringComplete = false;  // whether the string is complete.
int d_year;
byte d_month, d_day, t_hour, t_minute, t_second;

void setup() {
  
  Serial.begin(115200);
  delay(50);

  inputString.reserve(200);

  if (!rtc.begin()) {
    Serial.println("DS3231 not found");
    for (;;)
      ;
  }

  if (rtc.lostPower()) {  // выполнится при сбросе батарейки
    Serial.println("lost power!");
    rtc.setTime(COMPILE_TIME);
  }

  ledcAttach(LED_PIN, LEDC_BASE_FREQ, LEDC_TIMER_12_BIT);
  analogWrite(LED_PIN, 0);

  pinMode(LED_IND_PIN, OUTPUT);
  button.attachClick(singleClick);
  button.attachLongPressStop(longClick);

  // первая инициализация и запуск
  int mode = get_current_mode();
  int target_duty = get_target_duty(mode);

  current_duty = target_duty;
  current_mode = mode;

  ledcFade(LED_PIN, 0, target_duty, 3000);

  WiFi.begin(ssid, password);
  WiFi.setAutoReconnect(true);
  //WiFi.persistent(true);

  int conn_count = 0;
  while (WiFi.status() != WL_CONNECTED) {
    conn_count++;
    delay(1000);
    Serial.print(".");
    if (conn_count == 60) {
      break;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected.");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    NTP.begin(3);         // запустить и указать часовой пояс
    NTP.setPeriod(3600);  // период синхронизации в секундах
    if (NTP.updateNow()) {
      rtc.setTime(NTP.second(), NTP.minute(), NTP.hour(), NTP.day(), NTP.month(), NTP.year());
    }

    ntp_timer.startInterval(43200, []() {  // 12 часов
      if (NTP.updateNow()) {
        rtc.setTime(NTP.second(), NTP.minute(), NTP.hour(), NTP.day(), NTP.month(), NTP.year());
      }
    });
  }

  serial_timer.startInterval(1000, []() {
    Serial.println(rtc.getDateString() + " - " + rtc.getTimeString());
    Serial.print("current_duty: ");
    Serial.println(current_duty);
    Serial.print("current_mode: ");
    Serial.print(current_mode);
    Serial.print(" -> ");
    Serial.print(brights[current_mode - 1]);
    Serial.println("%");
  });

}

void loop() {

  button.tick();
  ntp_timer.tick();

  serialEvent();
  serial_timer.tick();
  if (stringComplete) {
    handle_serial_command();
  }

  if (auto_mode) {
    int mode = get_current_mode();
    if (mode != current_mode) {
      make_fade(mode);
    }
    digitalWrite(LED_IND_PIN, 1);
  } else {
    digitalWrite(LED_IND_PIN, 0);
  }

  delay(10);
}

void make_fade(int mode) {
  // считаем конечную скважность
  int target_duty = get_target_duty(mode);
  // считаем время перехода. для граничных время 15 минут, между по 3 минуты
  int fade_time = get_fade_time(mode);
  // делаем фэйд
  ledcFade(LED_PIN, current_duty, target_duty, fade_time);
  // сохраняем текущую скважность
  current_duty = target_duty;
  // устанавливаем новый режим
  current_mode = mode;
}

int get_current_mode() {
  int hour = rtc.getHours();
  int mode = 8;

  if (hour >= 7 && hour < 8) {
    mode = 1;  // 10%
  } else if (hour >= 8 && hour < 9) {
    mode = 2;  // 50%
  } else if (hour >= 9 && hour < 10) {
    mode = 3;  // 80%
  } else if (hour >= 10 && hour < 19) {
    mode = 4;  // 100%
  } else if (hour >= 19 && hour < 20) {
    mode = 5;  // 80%
  } else if (hour >= 20 && hour < 21) {
    mode = 6;  // 50%
  } else if (hour >= 21 && hour < 22) {
    mode = 7;  // 10%
  } else {     // Все остальное время (22-6)
    mode = 8;  // 0%
  }

  return mode;
}

int get_target_duty(int mode) {
  return int(LEDC_TARGET_DUTY * brights[mode - 1] / 100);
}

int get_fade_time(int mode) {
  return ((mode == 1 or mode == 8) ? fade_times[0] : fade_times[1]) * 1000 * 60;  //* 60 для минут
}

void serialEvent() {
  while (Serial.available()) {
    // get the new byte.
    char inChar = (char)Serial.read();
    // if the incoming character is a newline, set a flag so the main loop can do something about it.
    if (inChar == '\n') {
      stringComplete = true;
      return;
    }
    // add it to the inputString.
    inputString += inChar;
  }
}

String getValue(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = { 0, -1 };
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void handle_serial_command() {
  Serial.print("Input String : ");
  Serial.println(inputString);

  String command = "";
  command = getValue(inputString, ',', 0);

  if (command == "settime") {
    Serial.println();
    Serial.println("------------");
    Serial.println("Set the Time and Date of the DS3231 RTC Module.");
    Serial.println("Incoming settings data : ");

    d_year = getValue(inputString, ',', 1).toInt();
    d_month = getValue(inputString, ',', 2).toInt();
    d_day = getValue(inputString, ',', 3).toInt();
    t_hour = getValue(inputString, ',', 4).toInt();
    t_minute = getValue(inputString, ',', 5).toInt();
    t_second = getValue(inputString, ',', 6).toInt();

    Serial.print("- Year : ");
    Serial.println(d_year);
    Serial.print("- Month : ");
    Serial.println(d_month);
    Serial.print("- Day : ");
    Serial.println(d_day);
    Serial.print("- Hour : ");
    Serial.println(t_hour);
    Serial.print("- Minute : ");
    Serial.println(t_minute);
    Serial.print("- Second : ");
    Serial.println(t_second);

    Serial.println("Set Time and Date...");
    rtc.setTime(t_second, t_minute, t_hour, d_day, d_month, d_year);

    Serial.println("Setting the Time and Date has been completed.");
    Serial.println("------------");
    Serial.println();

  } else if (command == "setmode") {  // режим работы: auto/manual
    String value = getValue(inputString, ',', 1);
    if (value == "auto") {
      auto_mode = true;
    } else {
      auto_mode = false;
    }
  } else if (command == "currentmode") {  // режим лампы: 1-8
    int value = getValue(inputString, ',', 1).toInt();
    if (value >= 1 && value <= 8) {
      current_mode = value;
      int target_duty = get_target_duty(current_mode);
      ledcFade(LED_PIN, current_duty, target_duty, 5000);
      current_duty = target_duty;
    }
  }

  // clear the string:
  inputString = "";
  stringComplete = false;
}

void singleClick() {
  if (!auto_mode) {
    if (current_mode == 8) {
      current_mode = 1;
    } else {
      current_mode++;
    }
    int target_duty = get_target_duty(current_mode);
    ledcFade(LED_PIN, current_duty, target_duty, 3000);
    current_duty = target_duty;
  }
}

void longClick() {
  if (!auto_mode) {
    current_mode = get_current_mode();
    int target_duty = get_target_duty(current_mode);
    ledcFade(LED_PIN, current_duty, target_duty, 3000);
    current_duty = target_duty;
  }
  auto_mode = !auto_mode;
}
