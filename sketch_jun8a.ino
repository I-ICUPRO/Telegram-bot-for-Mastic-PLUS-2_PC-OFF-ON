#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <M5StickCPlus2.h>

const char* ssid = "Damoind"; // Замените на ваш SSID
const char* password = "11223344"; // Замените на ваш пароль
String BOTtoken = "1234567890:ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890"; // Замените на ваш токен
String CHAT_ID = "123456789"; // Замените на ваш ID чата

int relayPin = 26; // G26 для реле
WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);
int botRequestDelay = 500; // Интервал проверки сообщений (мс)
unsigned long lastTimeBotRan = 0;
unsigned long lastBatteryCheck = 0;
const unsigned long batteryCheckInterval = 5000; // Проверка батареи каждые 5 секунд

String wifiStatus = "Disconnected";
String ip = "N/A";
String lastCommand = "None";
bool computerOn = false; // Состояние компьютера
bool displayNeedsUpdate = true;
int batteryLevel = 0;
bool isCharging = false;
float lastBatteryVoltage = 0.0;

void updateDisplay() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(2);
  M5.Lcd.println("Wi-Fi: " + wifiStatus);
  M5.Lcd.println("IP: " + ip);
  M5.Lcd.println("Pin: G26");
  M5.Lcd.println("Last Cmd: " + lastCommand);
  M5.Lcd.println("Relay: " + String(computerOn ? "ON" : "OFF"));
  M5.Lcd.println("Battery: " + String(batteryLevel) + "%");
  M5.Lcd.println("Charging: " + String(isCharging ? "Yes" : "No"));
  displayNeedsUpdate = false;
}

void setup() {
  M5.begin();
  M5.Lcd.setRotation(3); // Настройте ориентацию (0-3)
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);
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
  isCharging = (lastBatteryVoltage > 4.2); // Порог 4.2V
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
  displayNeedsUpdate = true;
  updateDisplay();
}

void loop() {
  // Проверка Wi-Fi
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

  // Проверка батареи каждые 5 секунд
  if (millis() - lastBatteryCheck >= batteryCheckInterval) {
    int newBatteryLevel = M5.Power.getBatteryLevel();
    // Усреднение напряжения
    float tempVoltage = 0.0;
    for (int i = 0; i < 5; i++) {
      tempVoltage += M5.Power.getBatteryVoltage() / 1000.0;
      delay(10);
    }
    float newBatteryVoltage = tempVoltage / 5.0;
    bool newIsCharging = isCharging; // Сохраняем текущий статус
    if (newBatteryVoltage > 4.2) {
      newIsCharging = true; // Зарядка при >4.2V
    } else if (newBatteryVoltage < 4.1) {
      newIsCharging = false; // Нет зарядки при <4.1V
    } // Между 4.1V и 4.2V статус не меняется
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

  // Обработка сообщений Telegram
  if (millis() > lastTimeBotRan + botRequestDelay) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    if (numNewMessages) {
      for (int i = 0; i < numNewMessages; i++) {
        String chat_id = String(bot.messages[i].chat_id);
        if (chat_id != CHAT_ID) {
          bot.sendMessage(chat_id, "Unauthorized user", "");
          M5.Lcd.clear();
          M5.Lcd.println("Unauthorized");
          Serial.println("Unauthorized user: " + chat_id);
          delay(2000); // Показать на 2 секунды
          displayNeedsUpdate = true;
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
            delay(100); // Короткий импульс 100 мс
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
            delay(5000); // Длинный импульс 5 секунд
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
      }
    }
    lastTimeBotRan = millis();
  }

  if (displayNeedsUpdate) {
    updateDisplay();
  }
}