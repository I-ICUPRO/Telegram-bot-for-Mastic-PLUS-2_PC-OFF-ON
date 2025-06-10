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
int botRequestDelay = 500; // Уменьшено для быстрого ответа
unsigned long lastTimeBotRan;

void setup() {
  M5.begin();
  M5.Lcd.setRotation(3); // Попробуйте 0-3, если экран перевёрнут
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Booting...");
  Serial.begin(115200);
  Serial.println("Starting M5StickC+2...");
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);

  // Попробуем сначала без проверки сертификата
  client.setInsecure();
  M5.Lcd.println("Wi-Fi...");
  Serial.println("Connecting to Wi-Fi: " + String(ssid));
  WiFi.begin(ssid, password);
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
    delay(500);
    Serial.print(".");
    M5.Lcd.print(".");
    wifiAttempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    M5.Lcd.clear();
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("WiFi OK");
    M5.Lcd.println("IP: " + WiFi.localIP().toString());
    Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
    bot.sendMessage(CHAT_ID, "Bot started", "");
    Serial.println("Bot started, sent message to CHAT_ID: " + CHAT_ID);
  } else {
    M5.Lcd.clear();
    M5.Lcd.println("WiFi Failed");
    Serial.println("\nWiFi connection failed.");
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    M5.Lcd.clear();
    M5.Lcd.println("WiFi Lost");
    Serial.println("WiFi disconnected, reconnecting...");
    WiFi.reconnect();
    delay(5000);
    return;
  }
  if (millis() > lastTimeBotRan + botRequestDelay) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    if (numNewMessages) {
      Serial.println("Received " + String(numNewMessages) + " new messages");
      M5.Lcd.clear();
      M5.Lcd.setCursor(0, 0);
      M5.Lcd.println("Msg received");
      for (int i = 0; i < numNewMessages; i++) {
        String chat_id = String(bot.messages[i].chat_id);
        if (chat_id != CHAT_ID) {
          bot.sendMessage(chat_id, "Unauthorized user", "");
          M5.Lcd.println("Unauthorized");
          Serial.println("Unauthorized user: " + chat_id);
          continue;
        }
        String text = bot.messages[i].text;
        String from_name = bot.messages[i].from_name;
        if (from_name == "") from_name = "Guest";
        M5.Lcd.println("Cmd: " + text);
        Serial.println("Command: " + text + " from " + from_name);
        if (text == "/start") {
          String welcome = "Welcome, " + from_name + ".\n";
          welcome += "Commands:\n";
          welcome += "/turn_on : Turn on PC\n";
          welcome += "/turn_off : Relay off\n";
          welcome += "/status : Relay status";
          bot.sendMessage(chat_id, welcome, "");
          M5.Lcd.println("Sent Start");
          Serial.println("Sent welcome message");
        } else if (text == "/turn_on") {
          digitalWrite(relayPin, HIGH);
          delay(100);
          digitalWrite(relayPin, LOW);
          bot.sendMessage(chat_id, "Computer turned on", "");
          M5.Lcd.println("PC ON");
          Serial.println("Computer turned on");
        } else if (text == "/turn_off") {
          digitalWrite(relayPin, LOW);
          bot.sendMessage(chat_id, "Relay off", "");
          M5.Lcd.println("Relay OFF");
          Serial.println("Relay off");
        } else if (text == "/status") {
          bot.sendMessage(chat_id, "Relay is OFF", "");
          M5.Lcd.println("Relay OFF");
          Serial.println("Relay is OFF");
        } else {
          bot.sendMessage(chat_id, "Invalid command", "");
          M5.Lcd.println("Invalid cmd");
          Serial.println("Invalid command: " + text);
        }
      }
    }
    lastTimeBotRan = millis();
  }
}