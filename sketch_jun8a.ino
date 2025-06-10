#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <M5StickCPlus2.h>

// Настройки Wi-Fi и Telegram
const char* ssid = "Damoind"; // Замените на ваш SSID
const char* password = "11223344"; // Замените на ваш пароль
string BOTtoken = "1234567890:ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890"; // Замените на ваш токен
String CHAT_ID = "123456789"; // Замените на ваш ID чата

// Настройки пина и задержек реле
int relayPin = 26; // По умолчанию G26, можно сменить на G32 через /setpin
const unsigned long turnOnDelay = 300;  // Задержка для включения, мс
const unsigned long turnOffDelay = 300; // Задержка для выключения, мс

// Настройки батареи и зарядки
const float chargingHighThreshold = 4.2;
const float chargingLowThreshold = 4.0;
const unsigned long batteryCheckInterval = 5000;

// Настройки авто-отключения экрана
const unsigned long autoOffTimeout = 10000;

// Настройка размера шрифта
const uint8_t fontSize = 2;

// Настройка режима реле
bool isPulseMode = true; // true = импульсный, false = постоянный
bool isLowLevelTrigger = true; // true = low-level (LOW = включено)

// Глобальные переменные
WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);
int botRequestDelay = 500;
unsigned long lastTimeBotRan = 0;
unsigned long lastBatteryCheck = 0;
unsigned long lastActivityTime = 0;
String wifiStatus = "Disconnected";
String ip = "N/A";
String lastCommand = "None";
bool computerOn = false;
bool displayNeedsUpdate = true;
int batteryLevel = 0;
bool isCharging = false;
float lastBatteryVoltage = 0.0;
bool autoOffEnabled = false;
bool screenAsleep = false;

void updateDisplay() {
  if (screenAsleep) return;
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(fontSize);
  M5.Lcd.println("Wi-Fi: " + wifiStatus);
  M5.Lcd.println("IP: " + ip);
  M5.Lcd.println("Pin: G" + String(relayPin));
  M5.Lcd.println("Mode: " + String(isPulseMode ? "Pulse" : "Switch"));
  M5.Lcd.println("Trigger: " + String(isLowLevelTrigger ? "Low" : "High"));
  M5.Lcd.println("Last Cmd: " + lastCommand);
  M5.Lcd.println("Relay: " + String(computerOn ? "ON" : "OFF"));
  M5.Lcd.println("Battery: " + String(batteryLevel) + "%");
  M5.Lcd.println("Charging: " + String(isCharging ? "Yes" : "No"));
  M5.Lcd.println("Auto-Off: " + String(autoOffEnabled ? "ON" : "OFF"));
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
    Serial.println("Relay set to: ACTIVE, Pin G" + String(relayPin) + ": " + String(state) + ", Voltage: ~" + String(state == HIGH ? "3.3V" : "0V"));
  } else {
    pinMode(relayPin, INPUT); // Переводим в INPUT для минимизации тока
    Serial.println("Relay set to: INACTIVE, Pin G" + String(relayPin) + ": INPUT (floating), Voltage: ~0V");
  }
}

void setRelayPin(int newPin) {
  pinMode(relayPin, INPUT); // Отключаем старый пин
  relayPin = newPin;
  setRelayState(false); // Новый пин в выключенное состояние
  Serial.println("Relay pin changed to G" + String(relayPin));
}

void setup() {
  M5.begin();
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(fontSize);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Booting...");
  Serial.begin(115200);
  Serial.println("Starting M5StickC+2...");
  setRelayState(false); // Реле выключено (INPUT)

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
  Serial.println("Initial Is Charging: " + String(isCharging ? "Yes" : "No"));
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
    bot.sendMessage(CHAT_ID, "Bot started", "");
    Serial.println("WiFi connected. IP: " + ip);
  } else {
    wifiStatus = "Disconnected";
    ip = "N/A";
    Serial.println("WiFi connection failed.");
  }
  lastActivityTime = millis();
  displayNeedsUpdate = true;
  updateDisplay();
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
        String from_name = bot.messages[i].from_name;
        if (from_name == "") from_name = "Guest";
        lastCommand = text;
        Serial.println("Command: " + text + " from " + from_name);
        if (text == "/start") {
          String welcome = "Welcome, " + from_name + ".\n";
          welcome += "Commands:\n";
          welcome += "/turn_on : Turn on PC\n";
          welcome += "/turn_off : Turn off PC\n";
          welcome += "/status : Check PC status\n";
          welcome += "/mode : Toggle relay mode (Pulse/Switch)\n";
          welcome += "/trigger : Toggle relay trigger (Low/High)\n";
          welcome += "/setpin : Toggle relay pin (G26/G32)";
          bot.sendMessage(chat_id, welcome, "");
        } else if (text == "/setpin") {
          int newPin = (relayPin == 26) ? 32 : 26;
          setRelayPin(newPin);
          bot.sendMessage(chat_id, "Relay pin set to: G" + String(relayPin), "");
          displayNeedsUpdate = true;
          handleActivity();
        } else if (text == "/trigger") {
          isLowLevelTrigger = !isLowLevelTrigger;
          String trigger = isLowLevelTrigger ? "Low" : "High";
          bot.sendMessage(chat_id, "Relay trigger set to: " + trigger, "");
          Serial.println("Relay trigger set to: " + trigger);
          setRelayState(computerOn);
          displayNeedsUpdate = true;
          handleActivity();
        } else if (text == "/mode") {
          isPulseMode = !isPulseMode;
          String mode = isPulseMode ? "Pulse" : "Switch";
          bot.sendMessage(chat_id, "Relay mode set to: " + mode, "");
          Serial.println("Relay mode set to: " + mode);
          if (isPulseMode) {
            setRelayState(false);
          } else {
            setRelayState(computerOn);
          }
          displayNeedsUpdate = true;
          handleActivity();
        } else if (text == "/turn_on") {
          if (!computerOn) {
            if (isPulseMode) {
              setRelayState(true);
              delay(turnOnDelay);
              setRelayState(false);
            } else {
              setRelayState(true);
            }
            computerOn = true;
            bot.sendMessage(chat_id, "Computer turned on", "");
            Serial.println("Computer turned on");
          } else {
            bot.sendMessage(chat_id, "Computer is already ON", "");
            Serial.println("Computer already ON");
          }
        } else if (text == "/turn_off") {
          if (computerOn) {
            if (isPulseMode) {
              setRelayState(true);
              delay(turnOffDelay);
              setRelayState(false);
            } else {
              setRelayState(false);
            }
            computerOn = false;
            bot.sendMessage(chat_id, "Computer turned off", "");
            Serial.println("Computer turned off");
          } else {
            bot.sendMessage(chat_id, "Computer is already OFF", "");
            Serial.println("Computer already OFF");
          }
        } else if (text == "/status") {
          String mode = isPulseMode ? "Pulse" : "Switch";
          String trigger = isLowLevelTrigger ? "Low" : "High";
          bot.sendMessage(chat_id, "Computer is " + String(computerOn ? "ON" : "OFF") + "\nMode: " + mode + "\nTrigger: " + trigger + "\nPin: G" + String(relayPin), "");
          Serial.println("Status: Computer is " + String(computerOn ? "ON" : "OFF") + ", Mode: " + mode + ", Trigger: " + trigger + ", Pin: G" + String(relayPin));
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

  if (autoOffEnabled && !screenAsleep && (millis() - lastActivityTime > autoOffTimeout)) {
    M5.Lcd.sleep();
    screenAsleep = true;
    Serial.println("Screen asleep");
  }

  if (displayNeedsUpdate) {
    updateDisplay();
  }
}