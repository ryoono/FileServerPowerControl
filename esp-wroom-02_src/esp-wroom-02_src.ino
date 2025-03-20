#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include "secrets.h"  // WIFI_SSID, WIFI_PASSWORD等を定義

// ピン定義
const uint8_t PHOTOCOPLER_PIN = 14;  // IO14: UPC817CG(1番ピン)に接続
const uint8_t LED_PIN         = 16;  // IO16: LED(アノード)に接続

// タイミング設定
const unsigned long TRIGGER_DURATION = 1500;  // 1.5秒ON
const unsigned long BLINK_INTERVAL   = 500;   // 500ms周期で点滅

ESP8266WebServer server(80);

// 状態管理用
bool triggerActive = false;
unsigned long triggerStartTime = 0;
unsigned long lastBlinkTime = 0;
bool blinkState = false;

void setupWiFi() {
  // WiFi接続（SSID/PASSはsecrets.hから取得）
  WiFi.mode(WIFI_STA);
  WiFi.hostname("raspipow");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  Serial.print("WiFi接続中");
  // 接続完了までループ。ただし、接続失敗時は1分後に再試行
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    // LED点滅処理（非同期に行うため、ここでは短いdelay）
    if (millis() - lastBlinkTime >= BLINK_INTERVAL) {
      blinkState = !blinkState;
      digitalWrite(LED_PIN, blinkState ? HIGH : LOW);
      lastBlinkTime = millis();
    }
    // 10秒毎にシリアル出力
    if (millis() - startAttemptTime > 10000) {
      Serial.print(".");
      startAttemptTime = millis();
    }
    delay(10);
  }
  // 接続完了：LED消灯
  digitalWrite(LED_PIN, LOW);
  Serial.println();
  Serial.print("WiFi接続完了: ");
  Serial.println(WiFi.localIP());
}

void handleRoot() {
  // 初期表示ページ：操作ボタン有効
  String page = "<!DOCTYPE html><html lang='ja'><head><meta charset='UTF-8'>";
  page += "<title>RaspberryPi電源操作</title>";
  // CSS（淡い緑を基調）
  page += "<style>";
  page += "body { background-color: #e8f5e9; font-family: Arial, sans-serif; text-align: center; margin-top: 100px; }";
  page += ".btn { background-color: #a5d6a7; border: none; padding: 20px 40px; font-size: 24px; cursor: pointer; border-radius: 8px; }";
  page += ".btn:active { background-color: #81c784; }";
  page += "</style></head><body>";
  page += "<h1>RaspberryPi電源操作</h1>";
  page += "<form action='/toggle' method='POST'>";
  page += "<button class='btn' type='submit'>電源ON/OFF</button>";
  page += "</form>";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

void handleToggle() {
  // ボタン連打防止のため、すでに動作中の場合は何もせず初期ページを再表示
  if (triggerActive) {
    server.sendHeader("Refresh", "0; url=/");
    server.send(200, "text/html", "既に動作中です。ページを再読み込みしてください。");
    return;
  }
  
  // フォトカプラをONにする（UPC817CGを0.75秒間ON）と同時にLEDもON
  triggerActive = true;
  triggerStartTime = millis();
  digitalWrite(PHOTOCOPLER_PIN, HIGH);  // HIGHでGNDに落とす
  digitalWrite(LED_PIN, HIGH);         // LED ON
  
  // ボタン操作済みページ
  String page = "<!DOCTYPE html><html lang='ja'><head><meta charset='UTF-8'>";
  page += "<title>RaspberryPi電源操作</title>";
  // CSS（淡い緑を基調）
  page += "<style>";
  page += "body { background-color: #e8f5e9; font-family: Arial, sans-serif; text-align: center; margin-top: 100px; }";
  page += ".btn { background-color: #c8e6c9; border: none; padding: 20px 40px; font-size: 24px; border-radius: 8px; color: #777; }";
  page += "</style></head><body>";
  page += "<h1>RaspberryPi電源操作</h1>";
  page += "<button class='btn' type='button' disabled>電源ON/OFF</button>";
  page += "<p>操作済み。再度操作したい場合は、Webページを再読み込みしてください。</p>";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

void setup() {
  Serial.begin(115200);

  // ピン初期設定
  pinMode(PHOTOCOPLER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(PHOTOCOPLER_PIN, LOW);  // 通常は非アクティブ状態（LOW）
  digitalWrite(LED_PIN, LOW);           // LED OFF（接続完了後は消灯）

  // WiFi接続（接続失敗時は1分ごとに再試行するため、ここはsetupで一度試行）
  setupWiFi();

  // MDNS初期化
  if (MDNS.begin("raspipow")) {
    Serial.println("MDNS responder started");
  }

  // Webサーバー設定
  server.on("/", HTTP_GET, handleRoot);
  server.on("/toggle", HTTP_POST, handleToggle);
  server.begin();
  Serial.println("HTTPサーバー開始");
}

void loop() {
  // 再接続処理（WiFi切断時は1分ごとに再接続）
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastReconnectAttempt = 0;
    if (millis() - lastReconnectAttempt >= 60000) {  // 1分間隔
      Serial.println("WiFi切断中。再接続を試みます...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      lastReconnectAttempt = millis();
    }
    // 接続中はLEDで点滅（photocoupler動作中は優先）
    if (!triggerActive && millis() - lastBlinkTime >= BLINK_INTERVAL) {
      blinkState = !blinkState;
      digitalWrite(LED_PIN, blinkState ? HIGH : LOW);
      lastBlinkTime = millis();
    }
  } else {
    // WiFi接続済みの場合、かつ通常時はLEDはOFF（photocoupler動作中はLED ON）
    if (!triggerActive) {
      digitalWrite(LED_PIN, LOW);
    }
  }

  // photcoupler動作中のタイムアウト処理
  if (triggerActive && (millis() - triggerStartTime >= TRIGGER_DURATION)) {
    // 1.5秒経過後、元の状態に戻す
    digitalWrite(PHOTOCOPLER_PIN, LOW);  // 非アクティブ状態に戻す
    // WiFi接続済みならLED OFF、未接続なら点滅制御に任せる
    if (WiFi.status() == WL_CONNECTED) {
      digitalWrite(LED_PIN, LOW);
    }
    triggerActive = false;
  }

  server.handleClient();
  MDNS.update();
}
