#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <M5StickCPlus2.h>

// Настройки Wi-Fi и Telegram
const char* ssid = "Damoind"; // Замените на ваш SSID
const char* password = "11223344"; // Замените на ваш пароль
String BOTtoken = "1234567890:ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890"; // Замените на ваш токен
String CHAT_ID = "123456789"; // Замените на ваш ID чата

// Настройки пина и задержек реле
int relayPin = 26; // G26 для реле
const unsigned long turnOnDelay = 100;  // Задержка для включения, мс
const unsigned long turnOffDelay = 5000; // Задержка для выключения, мс

// Настройки батареи и зарядки
const float chargingHighThreshold = 4.2;  // Верхний порог для зарядки, В
const float chargingLowThreshold = 4.1;   // Нижний порог для зарядки, В
const unsigned long batteryCheckInterval = 5000; // Проверка батареи каждые 5 секунд

// Настройки авто-отключения экрана
const unsigned long autoOffTimeout = 10000; // Время бездействия до выключения экрана, мс (10 секунд)

// Настройка размера шрифта
const uint8_t fontSize = 2; // Размер шрифта (1 = мелкий, 2 = средний, 3 = крупный)

// Глобальные переменные
WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);
int botRequestDelay = 500; // Интервал проверки сообщений, мс
unsigned long lastTimeBotRan = 0;
unsigned long lastBatteryCheck = 0;
unsigned long lastActivityTime = 0; // Время последней активности
String wifiStatus = "Disconnected";
String ip = "N/A";
String lastCommand = "None";
bool computerOn = false; // Состояние компьютера
bool displayNeedsUpdate = true;
int batteryLevel = 0;
bool isCharging = false;
float lastBatteryVoltage = 0.0;
bool autoOffEnabled = false; // Авто-отключение экрана
bool screenAsleep = false;   // Состояние экрана (спит/не спит)

void updateDisplay() {
  if (screenAsleep) return; // Не обновляем, если экран спит
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(fontSize);
  M5.Lcd.println("Wi-Fi: " + wifiStatus);
  M5.Lcd.println("IP: " + ip);
  M5.Lcd.println("Pin: G26");
  M5.Lcd.println("Last Cmd: " + lastCommand);
  M5.Lcd.println("Relay: " + String(computerOn ? "ON" : "OFF"));
  M5.Lcd.println("Battery: " + String(batteryLevel) + "%");
  M5.Lcd.println("Charging: " + String(isCharging ? "Yes" : "No"));
  M5.Lcd.println("Auto-Off: " + String(autoOffEnabled ? "ON" : "OFF"));
  // Если текст не помещается, закомментируйте строки, например:
  // M5.Lcd.println("IP: " + ip);
  displayNeedsUpdate = false;
}

void handleActivity() {
  lastActivityTime = millis(); // Обновляем время активности
  if (screenAsleep) {          // Если экран спит
    M5.Lcd.wakeup();           // Будим экран
    screenAsleep = false;      // Обновляем состояние
    Serial.println("Screen woken up");
  }
  displayNeedsUpdate = true; // Всегда обновляем экран при активности
}

void setup() {
  M5.begin();
  M5.Lcd.setRotation(3); // Настройте ориентацию (0-3)
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(fontSize);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Booting...");
  Serial.begin(115200);
  Serial.println("Starting M5StickC+2...");
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);

  // Инициализация питания
  M5.Power.begin();
  batteryLevel = M5.Power.getBatteryLevel();
  // Усреднение начального напряжения
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

  client.setInsecure(); // Временно для тестирования
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
  lastActivityTime = millis(); // Инициализация времени активности
  displayNeedsUpdate = true;
  updateDisplay();
}

void loop() {
  M5.update(); // Обновляем состояние кнопок

  // Обработка нажатия центральной кнопки (BtnA)
  if (M5.BtnA.wasPressed()) {
    autoOffEnabled = !autoOffEnabled; // Переключаем авто-отключение
    Serial.println("Auto-Off: " + String(autoOffEnabled ? "ON" : "OFF"));
    handleActivity(); // Пробуждаем экран и обновляем
  }

  // Проверка Wi-Fi
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiStatus != "Disconnected") {
      wifiStatus = "Disconnected";
      ip = "N/A";
      bot.sendMessage(CHAT_ID, "Wi-Fi disconnected!", "");
      Serial.println("WiFi disconnected, reconnecting...");
      displayNeedsUpdate = true;
      // Не вызываем handleActivity(), чтобы не включать экран
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
    // Не вызываем handleActivity(), чтобы не включать экран
  }

  // Проверка батареи
  if (millis() - lastBatteryCheck >= batteryCheckInterval) {
    int newBatteryLevel = M5.Power.getBatteryLevel();
    // Усреднение напряжения
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
      // Не вызываем handleActivity(), чтобы не включать экран
    }
    lastBatteryCheck = millis();
  }

  // Обработка сообщений Telegram
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
          handleActivity(); // Пробуждаем экран для неавторизованного пользователя
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
          welcome += "/status : Check PC status";
          bot.sendMessage(chat_id, welcome, "");
        } else if (text == "/turn_on") {
          if (!computerOn) {
            digitalWrite(relayPin, HIGH);
            delay(turnOnDelay);
            digitalWrite(relayPin, LOW);
            computerOn = true;
            bot.sendMessage(chat_id, "Computer turned on", "");
            Serial.println("Computer turned on");
          } else {
            bot.sendMessage(chat_id, "Computer is already ON", "");
            Serial.println("Computer already ON");
          }
        } else if (text == "/turn_off") {
          if (computerOn) {
            digitalWrite(relayPin, HIGH);
            delay(turnOffDelay);
            digitalWrite(relayPin, LOW);
            computerOn = false;
            bot.sendMessage(chat_id, "Computer turned off", "");
            Serial.println("Computer turned off");
          } else {
            bot.sendMessage(chat_id, "Computer is already OFF", "");
            Serial.println("Computer already OFF");
          }
        } else if (text == "/status") {
          bot.sendMessage(chat_id, "Computer is " + String(computerOn ? "ON" : "OFF"), "");
          Serial.println("Status: Computer is " + String(computerOn ? "ON" : "OFF"));
        } else {
          bot.sendMessage(chat_id, "Invalid command", "");
          Serial.println("Invalid command: " + text);
        }
        displayNeedsUpdate = true;
        handleActivity(); // Пробуждаем экран для команды
      }
    }
    lastTimeBotRan = millis();
  }

  // Авто-отключение экрана
  if (autoOffEnabled && !screenAsleep && (millis() - lastActivityTime > autoOffTimeout)) {
    M5.Lcd.sleep();
    screenAsleep = true;
    Serial.println("Screen asleep");
  }

  if (displayNeedsUpdate) {
    updateDisplay();
  }
}