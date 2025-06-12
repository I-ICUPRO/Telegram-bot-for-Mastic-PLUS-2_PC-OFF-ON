#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <M5StickCPlus2.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Настройки Wi-Fi и Telegram
const char* ssid = "Damoind"; // Замените на ваш SSID
const char* password = "11223344"; // Замените на ваш пароль
String BOTtoken = "1234567890:ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890"; // Замените на ваш токен
String CHAT_ID = "123456789"; // Замените на ваш ID чата

// Настройки пина и задержки реле
int relayPin = 26; // По умолчанию G26, можно сменить на G32 через /setpin
const unsigned long pulseDelay = 300; // Длительность импульса для /pulse, мс

// Настройки батареи и зарядки
const float chargingHighThreshold = 4.2;
const float chargingLowThreshold = 4.0;
const unsigned long batteryCheckInterval = 5000;

// Настройки авто-отключения экрана
const unsigned long autoOffTimeout = 10000;

// Настройка размера шрифта
const float fontSize = 1.5;

// Настройка реле
bool isLowLevelTrigger = true; // true = low-level (LOW = включено)

// Глобальные переменные
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);
int botRequestDelay = 500;
unsigned long lastTimeBotRan = 0;
unsigned long lastBatteryCheck = 0;
unsigned long lastActivityTime = 0;
String wifiStatus = "Disconnected";
String ip = "N/A";
String lastCommand = "None";
bool computerOn = false; // Состояние реле (для /turn_on, /turn_off)
bool displayNeedsUpdate = true;
int batteryLevel = 0;
bool isCharging = false;
float lastBatteryVoltage = 0.0;
bool autoOffEnabled = true;
bool screenAsleep = false;
unsigned long bootTime = 0; // Время включения устройства

// NTP Client для синхронизации времени (Самара, UTC+4)
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 14400, 60000); // UTC+4, обновление каждые 60 сек

// Структура для расписаний
struct Schedule {
  int hour;
  int minute;
  String command;
};
Schedule schedules[10]; // До 10 расписаний
int scheduleCount = 0;

// Структура для таймеров
struct Timer {
  unsigned long startTime;
  unsigned long delay;
  String command;
};
Timer timers[10]; // До 10 таймеров
int timerCount = 0;

void updateDisplay() {
  if (screenAsleep) return;
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(fontSize);
  M5.Lcd.println("Wi-Fi: " + wifiStatus);
  M5.Lcd.println("IP: " + ip);
  M5.Lcd.println("Pin: G" + String(relayPin));
  M5.Lcd.println("Trigger: " + String(isLowLevelTrigger ? "Low" : "High"));
  M5.Lcd.println("Last Cmd: " + lastCommand);
  M5.Lcd.println("Relay: " + String(computerOn ? "ON" : "OFF"));
  M5.Lcd.println("Battery: " + String(batteryLevel) + "%");
  M5.Lcd.println("Charging: " + String(isCharging ? "Yes" : "No"));
  M5.Lcd.println("Auto-Off: " + String(autoOffEnabled ? "ON" : "OFF"));
  M5.Lcd.println("Time: " + String(timeClient.getFormattedTime()));
  displayNeedsUpdate = false;
}

void handleActivity() {
  lastActivityTime = millis();
  if (screenAsleep) {
    M5.Lcd.wakeup();
    screenAsleep = false;
    Serial.println("Screen woken up");
  }
  displayNeedsUpdate = true;
}

void setRelayState(bool activate) {
  if (activate) {
    pinMode(relayPin, OUTPUT);
    int state = isLowLevelTrigger ? LOW : HIGH;
    digitalWrite(relayPin, state);
    Serial.println("Relay set to: ACTIVE, Pin G" + String(relayPin) + ": " + String(state) + ", Voltage: ~" + (state == HIGH ? "3.00" : "0.00"));
  } else {
    pinMode(relayPin, INPUT); // Минимизируем микротоки
    Serial.println("Relay set to: OFF, Pin " + String(relayPin) + ": INPUT");
  }
}

void setRelayPin(int newPin) {
  pinMode(relayPin, INPUT);
  // Сохраняем текущее состояние реле
  bool wasActive = computerOn;
  relayPin = newPin;
  // Восстанавливаем состояние реле на новом пине
  setRelayState(wasActive);
  Serial.println("Relay pin changed to G" + String(relayPin));
}

void showCommands(String chat_id, String from_name) {
  String welcome = "Welcome, " + from_name + " to M5Stick.\n\n";
  welcome += "Commands:\n\n";
  welcome += "/start or /c : Show commands\n\n";
  welcome += "/turn_on : Turn on PC (switch ON)\n";
  welcome += "/turn_off : Turn off PC (switch OFF)\n";
  welcome += "/pulse : Send pulse (button)\n\n";
  welcome += "/status or /s : Check PC status\n";
  welcome += "/trigger : Toggle relay trigger (Low/High)\n";
  welcome += "/setpin : Toggle relay pin (G26/G32)\n\n";
  welcome += "/schedule <HH:MM> <command> : Set daily schedule.\n";
  welcome += "/remove_schedule <HH:MM> : Remove schedule.\n";
  welcome += "/list_schedules : List all schedules\n\n";
  welcome += "/timer <HH:MM> <command> : Set timer.\n";
  welcome += "/remove_timer <index> : Remove timer by index.\n";
  welcome += "/list_timers : List all timers\n\n";
  welcome += "/shutdown : Shutdown the stick";
  bot.sendMessage(chat_id, welcome, "");
}

void showStatus(String chat_id) {
  String trigger = isLowLevelTrigger ? "Low" : "High";
  bot.sendMessage(chat_id, "Computer is " + String(computerOn ? "ON" : "OFF") + "\nTrigger: " + trigger + "\nPin: G" + String(relayPin), "");
  Serial.println("Status: Computer is " + String(computerOn ? "ON" : "OFF") + ", Trigger: " + trigger + ", Pin: G" + String(relayPin));
}

void executeCommand(String command) {
  if (command == "pulse") {
    if (computerOn) {
      bot.sendMessage(CHAT_ID, "Cannot send pulse while ON", "");
      Serial.println("Cannot send pulse while ON");
    } else {
      setRelayState(true);
      delay(pulseDelay);
      setRelayState(false);
      bot.sendMessage(CHAT_ID, "Pulse sent", "");
      Serial.println("Pulse sent");
    }
  } else if (command == "turn_on") {
    if (!computerOn) {
      setRelayState(true);
      computerOn = true;
      bot.sendMessage(CHAT_ID, "SET ON", "");
      Serial.println("SET ON");
    }
  } else if (command == "turn_off") {
    if (computerOn) {
      setRelayState(false);
      computerOn = false;
      bot.sendMessage(CHAT_ID, "SET OFF", "");
      Serial.println("SET OFF");
    }
  }
}

void clearOldMessages() {
  // Получаем все сообщения, чтобы очистить очередь
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  while (numNewMessages > 0) {
    for (int i = 0; i < numNewMessages; i++) {
      Serial.println("Cleared old message: " + String(bot.messages[i].text));
    }
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
}

void setup() {
  M5.begin();
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(fontSize);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Booting...");
  Serial.begin(115200);
  Serial.println("Starting M5StickC Plus 2...");
  setRelayState(false);

  M5.Power.begin();
  batteryLevel = M5.Power.getBatteryLevel();
  float tempVoltage = 0.0;
  for (int i = 0; i < 5; i++) {
    tempVoltage += M5.Power.getBatteryVoltage() / 1000.0;
    delay(10);
  }
  lastBatteryVoltage = tempVoltage / 5.0;
  isCharging = (lastBatteryVoltage > chargingHighThreshold);
  Serial.println("Initial Battery Level: " + String(batteryLevel) + "%");
  Serial.println("Initial Battery Voltage: " + String(lastBatteryVoltage) + "V");
  Serial.println("Is Charging: " + String(isCharging ? "Yes" : "No"));
  Serial.println("Raw isCharging: " + String(M5.Power.isCharging() ? "Yes" : "No"));

  client.setInsecure();
  M5.Lcd.println("Wi-Fi...");
  Serial.println("Connecting to Wi-Fi: " + String(ssid));
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    M5.Lcd.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifiStatus = "Connected";
    ip = WiFi.localIP().toString();
    Serial.println("WiFi connected. IP: " + ip);
    timeClient.begin();
    // Ожидаем успешной синхронизации NTP
    int ntpAttempts = 0;
    while (!timeClient.update() && ntpAttempts < 5) {
      Serial.println("NTP sync attempt " + String(ntpAttempts + 1));
      delay(500);
      ntpAttempts++;
    }
    if (timeClient.isTimeSet()) {
      bootTime = timeClient.getEpochTime();
      Serial.println("NTP synced. BootTime: " + String(bootTime));
    } else {
      bootTime = millis() / 1000; // Fallback на локальное время
      Serial.println("NTP sync failed. Using millis: " + String(bootTime));
    }
    clearOldMessages(); // Очищаем старые сообщения
    bot.sendMessage(CHAT_ID, "Bot started", "");
  } else {
    wifiStatus = "Disconnected";
    ip = "N/A";
    Serial.println("WiFi connection failed.");
  }
  lastActivityTime = millis();
  displayNeedsUpdate = true;
  updateDisplay();
}

void insertSortedSchedule(int hour, int minute, String command) {
  int insertPos = scheduleCount;
  for (int i = 0; i < scheduleCount; i++) {
    if (schedules[i].hour > hour || (schedules[i].hour == hour && schedules[i].minute > minute)) {
      insertPos = i;
      break;
    }
  }
  for (int i = scheduleCount; i > insertPos; i--) {
    schedules[i] = schedules[i - 1];
  }
  schedules[insertPos] = {hour, minute, command};
  scheduleCount++;
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    autoOffEnabled = !autoOffEnabled;
    Serial.println("Auto-Off: " + String(autoOffEnabled ? "ON" : "OFF"));
    handleActivity();
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (wifiStatus != "Disconnected") {
      wifiStatus = "Disconnected";
      ip = "N/A";
      bot.sendMessage(CHAT_ID, "Wi-Fi disconnected!", "");
      Serial.println("WiFi disconnected, reconnecting...");
      displayNeedsUpdate = true;
    }
    WiFi.reconnect();
    delay(5000);
    return;
  } else if (wifiStatus != "Connected") {
    wifiStatus = "Connected";
    ip = WiFi.localIP().toString();
    bot.sendMessage(CHAT_ID, "Wi-Fi reconnected, IP: " + ip, "");
    Serial.println("WiFi reconnected. IP: " + ip);
    timeClient.begin();
    // Ожидаем успешной синхронизации NTP
    int ntpAttempts = 0;
    while (!timeClient.update() && ntpAttempts < 5) {
      Serial.println("NTP sync attempt " + String(ntpAttempts + 1));
      delay(500);
      ntpAttempts++;
    }
    if (timeClient.isTimeSet()) {
      bootTime = timeClient.getEpochTime();
      Serial.println("NTP synced. BootTime: " + String(bootTime));
    } else {
      bootTime = millis() / 1000;
      Serial.println("NTP sync failed. Using millis: " + String(bootTime));
    }
    clearOldMessages(); // Очищаем старые сообщения
    displayNeedsUpdate = true;
  }

  if (millis() - lastBatteryCheck >= batteryCheckInterval) {
    int newBatteryLevel = M5.Power.getBatteryLevel();
    float tempVoltage = 0.0;
    for (int i = 0; i < 5; i++) {
      tempVoltage += M5.Power.getBatteryVoltage() / 1000.0;
      delay(10);
    }
    float newBatteryVoltage = tempVoltage / 5.0;
    bool newIsCharging = isCharging;
    if (newBatteryVoltage > chargingHighThreshold) {
      newIsCharging = true;
    } else if (newBatteryVoltage < chargingLowThreshold) {
      newIsCharging = false;
    }
    Serial.println("Battery Level: " + String(newBatteryLevel) + "%");
    Serial.println("Battery Voltage: " + String(newBatteryVoltage) + "V");
    Serial.println("Is Charging: " + String(newIsCharging ? "Yes" : "No"));
    Serial.println("Raw isCharging: " + String(M5.Power.isCharging() ? "Yes" : "No"));
    if (newBatteryLevel != batteryLevel || newIsCharging != isCharging) {
      batteryLevel = newBatteryLevel;
      isCharging = newIsCharging;
      lastBatteryVoltage = newBatteryVoltage;
      displayNeedsUpdate = true;
    }
    lastBatteryCheck = millis();
  }

  if (millis() > lastTimeBotRan + botRequestDelay) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    if (numNewMessages) {
      for (int i = 0; i < numNewMessages; i++) {
        String chat_id = String(bot.messages[i].chat_id);
        if (chat_id != CHAT_ID) {
          bot.sendMessage(chat_id, "Unauthorized user", "");
          M5.Lcd.clear();
          M5.Lcd.setTextSize(fontSize);
          M5.Lcd.println("Unauthorized");
          Serial.println("Unauthorized user: " + chat_id);
          delay(2000);
          displayNeedsUpdate = true;
          handleActivity();
          continue;
        }
        String text = bot.messages[i].text;
        long messageTime = bot.messages[i].date.toInt();
        String from_name = bot.messages[i].from_name;
        if (from_name == "") from_name = "Guest";
        Serial.println("Received message: " + text + ", Time: " + String(messageTime) + ", BootTime: " + String(bootTime));
        lastCommand = text; // Обновляем lastCommand для всех сообщений
        Serial.println("Processing command: " + text + " from " + from_name);
        if (text == "/start" || text == "/c") {
          showCommands(chat_id, from_name);
        } else if (text == "/setpin") {
          int newPin = (relayPin == 26) ? 32 : 26;
          setRelayPin(newPin);
          bot.sendMessage(chat_id, "Relay pin set to: G" + String(relayPin), "");
        } else if (text == "/trigger") {
          isLowLevelTrigger = !isLowLevelTrigger;
          String trigger = isLowLevelTrigger ? "Low" : "High";
          bot.sendMessage(chat_id, "Relay trigger set to: " + trigger, "");
          Serial.println("Relay trigger set to: " + trigger);
          setRelayState(computerOn);
        } else if (text == "/pulse") {
          executeCommand("pulse");
        } else if (text == "/turn_on") {
          executeCommand("turn_on");
        } else if (text == "/turn_off") {
          executeCommand("turn_off");
        } else if (text == "/status" || text == "/s") {
          showStatus(chat_id);
        } else if (text.startsWith("/schedule")) {
          String params = text.substring(10);
          int spaceIndex = params.indexOf(' ');
          String timeStr = params.substring(0, spaceIndex);
          String command = params.substring(spaceIndex + 1);
          int colonIndex = timeStr.indexOf(':');
          if (colonIndex == -1 || spaceIndex == -1) {
            bot.sendMessage(chat_id, "Invalid format. Use /schedule HH:MM pulse|turn_on|turn_off", "");
          } else {
            int hour = timeStr.substring(0, colonIndex).toInt();
            int minute = timeStr.substring(colonIndex + 1).toInt();
            if (scheduleCount < 10 && (command == "pulse" || command == "turn_on" || command == "turn_off") && hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59) {
              insertSortedSchedule(hour, minute, command);
              bot.sendMessage(chat_id, "Schedule set for " + timeStr + " to execute " + command, "");
            } else {
              bot.sendMessage(chat_id, "Invalid time or command. Use /schedule HH:MM pulse|turn_on|turn_off", "");
            }
          }
        } else if (text.startsWith("/remove_schedule")) {
          String timeStr = text.substring(16);
          int colonIndex = timeStr.indexOf(':');
          if (colonIndex == -1) {
            bot.sendMessage(chat_id, "Invalid format. Use /remove_schedule HH:MM", "");
          } else {
            int hour = timeStr.substring(0, colonIndex).toInt();
            int minute = timeStr.substring(colonIndex + 1).toInt();
            bool found = false;
            for (int j = 0; j < scheduleCount; j++) {
              if (schedules[j].hour == hour && schedules[j].minute == minute) {
                for (int k = j; k < scheduleCount - 1; k++) {
                  schedules[k] = schedules[k + 1];
                }
                scheduleCount--;
                bot.sendMessage(chat_id, "Schedule at " + timeStr + " removed", "");
                found = true;
                break;
              }
            }
            if (!found) {
              bot.sendMessage(chat_id, "No schedule found at " + timeStr, "");
            }
          }
        } else if (text == "/list_schedules") {
          String list = "Schedules:\n";
          for (int j = 0; j < scheduleCount; j++) {
            list += String(schedules[j].hour) + ":" + (schedules[j].minute < 10 ? "0" : "") + String(schedules[j].minute) + " - " + schedules[j].command + "\n";
          }
          bot.sendMessage(chat_id, list == "Schedules:\n" ? "No schedules set" : list, "");
        } else if (text.startsWith("/timer")) {
          String params = text.substring(7);
          int spaceIndex = params.indexOf(' ');
          String timeStr = params.substring(0, spaceIndex);
          String command = params.substring(spaceIndex + 1);
          int colonIndex = timeStr.indexOf(':');
          if (colonIndex == -1 || spaceIndex == -1) {
            bot.sendMessage(chat_id, "Invalid format. Use /timer HH:MM pulse|turn_on|turn_off", "");
          } else {
            int hours = timeStr.substring(0, colonIndex).toInt();
            int minutes = timeStr.substring(colonIndex + 1).toInt();
            if (timerCount < 10 && (command == "pulse" || command == "turn_on" || command == "turn_off") && hours >= 0 && hours <= 23 && minutes >= 0 && minutes <= 59) {
              unsigned long delay = hours * 3600000UL + minutes * 60000UL;
              if (delay > 0) {
                timers[timerCount] = {millis(), delay, command};
                timerCount++;
                bot.sendMessage(chat_id, "Timer set for " + timeStr + " to execute " + command, "");
              } else {
                bot.sendMessage(chat_id, "Timer duration must be greater than 0", "");
              }
            } else {
              bot.sendMessage(chat_id, "Invalid time or command. Use /timer HH:MM pulse|turn_on|turn_off", "");
            }
          }
        } else if (text.startsWith("/remove_timer")) {
          String indexStr = text.substring(13);
          int index = indexStr.toInt() - 1;
          if (index >= 0 && index < timerCount) {
            for (int j = index; j < timerCount - 1; j++) {
              timers[j] = timers[j + 1];
            }
            timerCount--;
            bot.sendMessage(chat_id, "Timer " + String(index + 1) + " removed", "");
          } else {
            bot.sendMessage(chat_id, "Invalid timer index. Check /list_timers", "");
          }
        } else if (text == "/list_timers") {
          String list = "Timers:\n";
          for (int j = 0; j < timerCount; j++) {
            unsigned long remainingMs = timers[j].delay - (millis() - timers[j].startTime);
            int hours = remainingMs / 3600000;
            int minutes = (remainingMs % 3600000) / 60000;
            list += "Timer " + String(j + 1) + ": " + String(hours) + ":" + (minutes < 10 ? "0" : "") + String(minutes) + " - " + timers[j].command + "\n";
          }
          bot.sendMessage(chat_id, list == "Timers:\n" ? "No timers set" : list, "");
        } else if (text == "/shutdown") {
          bot.sendMessage(chat_id, "Shutting down...", "");
          M5.Power.deepSleep();
        } else {
          bot.sendMessage(chat_id, "Invalid command", "");
          Serial.println("Invalid command: " + text);
        }
        displayNeedsUpdate = true;
        handleActivity();
      }
    }
    lastTimeBotRan = millis();
  }

  // Проверка расписаний
  timeClient.update();
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();
  static int lastCheckedMinute = -1; // Для предотвращения многократного срабатывания
  if (currentMinute != lastCheckedMinute) {
    for (int i = 0; i < scheduleCount; i++) {
      if (schedules[i].hour == currentHour && schedules[i].minute == currentMinute) {
        executeCommand(schedules[i].command);
      }
    }
    lastCheckedMinute = currentMinute;
  }

  // Проверка таймеров
  for (int i = 0; i < timerCount; i++) {
    if (millis() - timers[i].startTime >= timers[i].delay) {
      executeCommand(timers[i].command);
      // Удаляем таймер
      for (int j = i; j < timerCount - 1; j++) {
        timers[j] = timers[j + 1];
      }
      timerCount--;
      i--;
    }
  }

  if (autoOffEnabled && !screenAsleep && (millis() - lastActivityTime > autoOffTimeout)) {
    M5.Lcd.sleep();
    screenAsleep = true;
    Serial.println("Screen asleep");
  }

  if (displayNeedsUpdate) {
    updateDisplay();
  }
}