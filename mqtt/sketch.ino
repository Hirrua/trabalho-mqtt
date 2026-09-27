
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <time.h>
#include "driver/gpio.h"
#include <Preferences.h>

#define NOME "hirrua"

#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASS ""

#define DHTPIN 4
#define DHTTYPE DHT22
#define LED 2

#define JANELA_CMD_MS   5000
#define DORMIR_SEG      30

DHT dht(DHTPIN, DHTTYPE);
Preferences nvs;
WiFiClient wifi;
PubSubClient mqtt(wifi);

const String BASE = String("sis1a/") + NOME + "/";

RTC_DATA_ATTR int   ciclos     = 0;
RTC_DATA_ATTR float limite     = 30.0;
RTC_DATA_ATTR int   modoLed    = 0;
RTC_DATA_ATTR bool  ledAnterior = false;

bool  ledOn = false;
bool  sensoresLidos = false;
bool  comandoRecebido = false;
float ultimaTemp = NAN;
long  tempoConexao = 0;

void pub(const String& sub, const String& valor, bool retido = true) {
  mqtt.publish((BASE + sub).c_str(), valor.c_str(), retido);
  Serial.printf("  PUB %s%s = %s\n", BASE.c_str(), sub.c_str(), valor.c_str());
}

String horaLocal() {
  struct tm t;
  if (!getLocalTime(&t, 3000)) return "sem-hora";
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
  return String(buf);
}

String motivoAcordar() {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER: return "timer";
    case ESP_SLEEP_WAKEUP_UNDEFINED: return "reset";
    default: return "outro";
  }
}

void aplicarLed() {
  if (modoLed == 1) ledOn = true;
  else if (modoLed == 2) ledOn = false;
  else ledOn = (!isnan(ultimaTemp) && ultimaTemp > limite);

  digitalWrite(LED, ledOn ? HIGH : LOW);

  if (ledOn != ledAnterior || ciclos == 1) {
    pub("led", ledOn ? "ON" : "OFF");
    ledAnterior = ledOn;
  }
}

void aoReceber(char* topico, byte* payload, unsigned int len) {
  String msg;
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  msg.trim();
  Serial.printf("RECEBIDO [%s]: %s\n", topico, msg.c_str());
  comandoRecebido = true;

  if (msg.startsWith("limite:")) {
    limite = msg.substring(7).toFloat();
    modoLed = 0;
    Serial.printf(" -> novo limite = %.1f (modo AUTO)\n", limite);
    pub("limite", String(limite, 1));
  } else if (msg.startsWith("led:")) {
    String e = msg.substring(4);
    e.toUpperCase();
    if (e == "ON" || e == "1") modoLed = 1;
    else if (e == "OFF" || e == "0") modoLed = 2;
    else modoLed = 0;
    Serial.printf(" -> modo LED = %s\n", modoLed == 1 ? "ON" : modoLed == 2 ? "OFF" : "AUTO");
  }
  if (sensoresLidos) aplicarLed();
}

void conectarWifi() {
  unsigned long t0 = millis();
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(200); Serial.print("."); }
  tempoConexao = millis() - t0;
  Serial.printf("\n ok | IP=%s | RSSI=%d dBm | %ld ms\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI(), tempoConexao);
}

void conectarMqtt() {
  String id = String("esp32-") + NOME + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  while (!mqtt.connected()) {
    Serial.print("Conectando ao Mosquitto...");
    if (mqtt.connect(id.c_str())) {
      Serial.println(" ok");
      mqtt.subscribe((BASE + "comando").c_str());
    } else {
      Serial.printf(" falhou rc=%d (2s)\n", mqtt.state());
      delay(2000);
    }
  }
}

void publicarSensores() {
  ultimaTemp = dht.readTemperature();
  float h = dht.readHumidity();
  pub("temp", isnan(ultimaTemp) ? "erro" : String(ultimaTemp, 1));
  pub("umid", isnan(h) ? "erro" : String(h, 1));
}

void publicarLogs() {
  String ssid = WiFi.SSID();
  int rssi = WiFi.RSSI();
  String ip = WiFi.localIP().toString();

  pub("wifi", ssid);
  pub("rssi", String(rssi));
  pub("ip", ip);
  pub("log/wifi", "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(rssi) +
                  ",\"ip\":\"" + ip + "\",\"ms\":" + String(tempoConexao) + "}");

  String hora = horaLocal();
  bool relogioOk = (hora != "sem-hora");
  pub("hora", hora);
  pub("log/tls", "{\"hora\":\"" + hora + "\",\"relogio_ok\":" + (relogioOk ? "true" : "false") + "}");

  pub("ciclos", String(ciclos));
  pub("log/sistema", "{\"ciclos\":" + String(ciclos) +
                     ",\"heap\":" + String(ESP.getFreeHeap()) +
                     ",\"acordou_por\":\"" + motivoAcordar() + "\"}");
}

void dormir() {
  Serial.printf("Dormindo %d s...\n\n", DORMIR_SEG);
  mqtt.disconnect();
  WiFi.disconnect(true);
  gpio_hold_en((gpio_num_t)LED);
  gpio_deep_sleep_hold_en();
  esp_sleep_enable_timer_wakeup((uint64_t)DORMIR_SEG * 1000000ULL);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  gpio_hold_dis((gpio_num_t)LED);
  pinMode(LED, OUTPUT);
  dht.begin();

  nvs.begin("estacao", false);
  bool rtcPerdida = (ciclos == 0);
  if (rtcPerdida) ciclos = nvs.getInt("ciclos", 0);
  ciclos++;
  nvs.putInt("ciclos", ciclos);
  nvs.end();
  if (rtcPerdida && ciclos > 1) Serial.println("RTC zerada: contador restaurado da NVS");
  Serial.printf("\n===== Ciclo %d (acordou por: %s) =====\n", ciclos, motivoAcordar().c_str());

  conectarWifi();
  configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  mqtt.setServer("test.mosquitto.org", 1883);
  mqtt.setCallback(aoReceber);
  conectarMqtt();

  unsigned long tr = millis();
  while (!comandoRecebido && millis() - tr < 4000) { mqtt.loop(); delay(10); }

  publicarSensores();
  sensoresLidos = true;
  aplicarLed();
  publicarLogs();

  Serial.printf("Janela de %d ms para receber comando...\n", JANELA_CMD_MS);
  unsigned long t0 = millis();
  while (millis() - t0 < JANELA_CMD_MS) { mqtt.loop(); delay(10); }

  dormir();
}

void loop() {}