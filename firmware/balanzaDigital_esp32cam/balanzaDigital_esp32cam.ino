/**
 * balanzaDigital — firmware ESP32-CAM
 *
 * Lo que hace la placa:
 *   1. Se conecta al WiFi y al broker MQTT del servidor.
 *   2. Se anuncia en `bascula/{id}/estado` como mensaje RETENIDO. Ese
 *      anuncio es todo el "descubrimiento": el backend lo lee y la báscula
 *      aparece en la web sin que nadie configure una IP a mano.
 *   3. Sirve el vídeo en :81/stream y el disparo en :80/capture.
 *   4. Escucha órdenes en `bascula/{id}/comando`.
 *
 * El peso NO se calcula aquí: la placa solo da imagen. El OCR lo hace el
 * backend, que tiene CPU de sobra y se puede afinar sin reflashear 20 placas.
 */
#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <esp_camera.h>

#include "pines_camara.h"
#include "secrets.h"
#include "servidor_web.h"

#define VERSION_FW "1.0.0"

static WiFiClient clienteWifi;
static PubSubClient mqtt(clienteWifi);

static char topicEstado[96];
static char topicDisponibilidad[96];
static char topicComando[96];

static unsigned long ultimoAnuncio = 0;
static const unsigned long INTERVALO_ANUNCIO_MS = 60000;

// ── Cámara ──────────────────────────────────────────────────────────────

static bool iniciarCamara() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    // SVGA (800x600) es el punto dulce: los dígitos del display salen con
    // píxeles de sobra para el OCR y el JPEG sigue cabiendo en RAM.
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 10;  // 10-12 en esta escala; más bajo = mejor calidad
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    // Sin PSRAM no da para más. Si acabas aquí, revisa que la placa la lleve:
    // leer un display a VGA con calidad 12 es jugársela.
    log_w("Sin PSRAM: se baja a VGA");
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    log_e("esp_camera_init falló: 0x%x", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    // Un display retroiluminado engaña al automático: la cámara expone para
    // el fondo oscuro de la nave y el display sale quemado y blanco. Bajar
    // la exposición y el brillo es lo que hace legibles los dígitos.
    s->set_brightness(s, -1);
    s->set_contrast(s, 2);
    s->set_saturation(s, -2);
    s->set_gainceiling(s, GAINCEILING_2X);
    s->set_ae_level(s, -1);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    // Sin corrección de lente los bordes se curvan y el ROI deja de cuadrar.
    s->set_lenc(s, 1);
  }

  return true;
}

/** Pulso corto de flash. Nunca fijo: recalienta y deslumbra el display. */
static void destelloFlash(uint16_t ms) {
  digitalWrite(FLASH_GPIO_NUM, HIGH);
  delay(ms);
  digitalWrite(FLASH_GPIO_NUM, LOW);
}

// ── WiFi ────────────────────────────────────────────────────────────────

static void conectarWifi() {
  WiFi.mode(WIFI_STA);
  // El ahorro de energía del WiFi mete latencias de cientos de ms en el
  // stream. En una instalación fija con alimentación no compensa.
  WiFi.setSleep(false);
  WiFi.setHostname(BASCULA_ID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Conectando a %s", WIFI_SSID);
  uint8_t intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 60) {
    delay(500);
    Serial.print('.');
    intentos++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nSin WiFi. Reiniciando.");
    ESP.restart();
  }

  Serial.printf("\nWiFi OK · IP %s · RSSI %d dBm\n", WiFi.localIP().toString().c_str(),
                WiFi.RSSI());
}

// ── MQTT ────────────────────────────────────────────────────────────────

/** Anuncio retenido. Es lo que da de alta la báscula en el backend. */
static void publicarEstado() {
  JsonDocument doc;
  doc["id"] = BASCULA_ID;
  doc["nombre"] = BASCULA_NOMBRE;
  doc["ip"] = WiFi.localIP().toString();
  doc["puerto"] = 80;
  doc["puertoStream"] = 81;
  doc["rssi"] = WiFi.RSSI();
  doc["fw"] = VERSION_FW;
  doc["modelo"] = "esp32cam-ai-thinker";

  sensor_t *s = esp_camera_sensor_get();
  doc["resolucion"] = s ? s->status.framesize : -1;
  doc["frames"] = fotogramasServidos();
  doc["uptime"] = millis() / 1000;

  char payload[320];
  size_t n = serializeJson(doc, payload, sizeof(payload));

  mqtt.publish(topicEstado, (const uint8_t *)payload, n, true);
  ultimoAnuncio = millis();
}

static void alRecibirComando(char *topic, byte *payload, unsigned int longitud) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, longitud)) {
    Serial.println("Comando ilegible");
    return;
  }

  const char *accion = doc["accion"] | "";
  Serial.printf("Comando recibido: %s\n", accion);

  if (!strcmp(accion, "reiniciar")) {
    mqtt.publish(topicDisponibilidad, "offline", true);
    delay(200);
    ESP.restart();

  } else if (!strcmp(accion, "flash")) {
    destelloFlash(doc["ms"] | 150);

  } else if (!strcmp(accion, "resolucion")) {
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
      s->set_framesize(s, (framesize_t)(doc["valor"] | FRAMESIZE_SVGA));
      // El sensor tarda un par de fotogramas en estabilizarse tras el cambio.
      delay(300);
      publicarEstado();
    }

  } else if (!strcmp(accion, "estado")) {
    publicarEstado();
  }
}

static void conectarMqtt() {
  while (!mqtt.connected()) {
    Serial.print("Conectando a MQTT... ");

    // El LWT es lo que hace que la web marque la báscula en rojo cuando se
    // va la corriente: el broker publica "offline" por nosotros.
    bool ok = mqtt.connect(BASCULA_ID, MQTT_USER, MQTT_PASSWORD, topicDisponibilidad, 1, true,
                           "offline");

    if (ok) {
      Serial.println("OK");
      mqtt.publish(topicDisponibilidad, "online", true);
      mqtt.subscribe(topicComando, 1);
      publicarEstado();
    } else {
      Serial.printf("falló (rc=%d), reintento en 5 s\n", mqtt.state());
      delay(5000);
    }
  }
}

// ── Arranque ────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  Serial.printf("\n\nbalanzaDigital · %s · fw %s\n", BASCULA_ID, VERSION_FW);

  pinMode(FLASH_GPIO_NUM, OUTPUT);
  digitalWrite(FLASH_GPIO_NUM, LOW);

  snprintf(topicEstado, sizeof(topicEstado), "%s/%s/estado", MQTT_TOPIC_PREFIX, BASCULA_ID);
  snprintf(topicDisponibilidad, sizeof(topicDisponibilidad), "%s/%s/disponibilidad",
           MQTT_TOPIC_PREFIX, BASCULA_ID);
  snprintf(topicComando, sizeof(topicComando), "%s/%s/comando", MQTT_TOPIC_PREFIX, BASCULA_ID);

  if (!iniciarCamara()) {
    Serial.println("Cámara KO. Reiniciando en 5 s.");
    delay(5000);
    ESP.restart();
  }

  conectarWifi();

  if (iniciarServidorWeb() != ESP_OK) {
    Serial.println("Servidor HTTP KO. Reiniciando en 5 s.");
    delay(5000);
    ESP.restart();
  }

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(alRecibirComando);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(30);
  conectarMqtt();

  Serial.printf("Listo. Stream en http://%s:81/stream\n", WiFi.localIP().toString().c_str());
  destelloFlash(80);  // Señal visual de que arrancó bien.
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi caído. Reiniciando.");
    ESP.restart();
  }

  if (!mqtt.connected()) conectarMqtt();
  mqtt.loop();

  // Reanuncio periódico: si el broker pierde los retenidos o el backend se
  // reinstala desde cero, la báscula vuelve a aparecer sin tocar nada.
  if (millis() - ultimoAnuncio > INTERVALO_ANUNCIO_MS) publicarEstado();

  delay(10);
}
