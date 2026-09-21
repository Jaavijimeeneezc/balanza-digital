#include <Arduino.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <esp_http_server.h>
#include <esp_timer.h>

#include "pines_camara.h"
#include "secrets.h"
#include "servidor_web.h"

static httpd_handle_t servidorControl = NULL;
static httpd_handle_t servidorStream = NULL;
static uint32_t contadorFotogramas = 0;

#define LIMITE_MJPEG "123456789000000000000987654321"
static const char *TIPO_STREAM = "multipart/x-mixed-replace;boundary=" LIMITE_MJPEG;
static const char *LIMITE_STREAM = "\r\n--" LIMITE_MJPEG "\r\n";
static const char *CABECERA_PARTE = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

uint32_t fotogramasServidos() { return contadorFotogramas; }

/**
 * Un disparo. Descarta el primer fotograma porque el sensor arrastra el
 * anterior en el buffer y, tras un rato quieto, sale con la exposición del
 * momento en que se llenó, no del actual.
 */
static esp_err_t manejadorCaptura(httpd_req_t *req) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb) esp_camera_fb_return(fb);

  fb = esp_camera_fb_get();
  if (!fb) {
    log_e("Fallo al capturar");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=captura.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  contadorFotogramas++;

  esp_camera_fb_return(fb);
  return res;
}

static esp_err_t manejadorStream(httpd_req_t *req) {
  esp_err_t res = httpd_resp_set_type(req, TIPO_STREAM);
  if (res != ESP_OK) return res;

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "X-Framerate", "20");

  char cabecera[64];

  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      log_e("Fotograma perdido en el stream");
      res = ESP_FAIL;
      break;
    }

    res = httpd_resp_send_chunk(req, LIMITE_STREAM, strlen(LIMITE_STREAM));
    if (res == ESP_OK) {
      size_t largo = snprintf(cabecera, sizeof(cabecera), CABECERA_PARTE, fb->len);
      res = httpd_resp_send_chunk(req, cabecera, largo);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    }

    esp_camera_fb_return(fb);

    // El cliente cerró la pestaña: es la salida normal del bucle.
    if (res != ESP_OK) break;

    contadorFotogramas++;
    // Sin esta pausa el watchdog salta y la placa se reinicia sola.
    vTaskDelay(pdMS_TO_TICKS(40));
  }

  return res;
}

static esp_err_t manejadorEstado(httpd_req_t *req) {
  sensor_t *s = esp_camera_sensor_get();
  char json[320];

  snprintf(json, sizeof(json),
           "{\"id\":\"%s\",\"nombre\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,"
           "\"framesize\":%d,\"calidad\":%d,\"frames\":%lu,"
           "\"heap\":%lu,\"uptime\":%lu}",
           BASCULA_ID, BASCULA_NOMBRE, WiFi.localIP().toString().c_str(), WiFi.RSSI(),
           s ? s->status.framesize : -1, s ? s->status.quality : -1,
           (unsigned long)contadorFotogramas, (unsigned long)ESP.getFreeHeap(),
           (unsigned long)(esp_timer_get_time() / 1000000));

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json, strlen(json));
}

static esp_err_t manejadorSalud(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "ok", 2);
}

esp_err_t iniciarServidorWeb() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 8;
  config.lru_purge_enable = true;
  // Una captura con poca luz puede tardar; sin margen el cliente ve un corte.
  config.recv_wait_timeout = 10;
  config.send_wait_timeout = 10;

  httpd_uri_t uriCaptura = {"/capture", HTTP_GET, manejadorCaptura, NULL};
  httpd_uri_t uriEstado = {"/status", HTTP_GET, manejadorEstado, NULL};
  httpd_uri_t uriSalud = {"/health", HTTP_GET, manejadorSalud, NULL};
  httpd_uri_t uriStream = {"/stream", HTTP_GET, manejadorStream, NULL};

  config.server_port = 80;
  config.ctrl_port = 32768;
  if (httpd_start(&servidorControl, &config) != ESP_OK) {
    log_e("No se pudo arrancar el servidor de control");
    return ESP_FAIL;
  }
  httpd_register_uri_handler(servidorControl, &uriCaptura);
  httpd_register_uri_handler(servidorControl, &uriEstado);
  httpd_register_uri_handler(servidorControl, &uriSalud);

  config.server_port = 81;
  config.ctrl_port = 32769;
  if (httpd_start(&servidorStream, &config) != ESP_OK) {
    log_e("No se pudo arrancar el servidor de stream");
    return ESP_FAIL;
  }
  httpd_register_uri_handler(servidorStream, &uriStream);

  log_i("HTTP listo: :80/capture  :81/stream");
  return ESP_OK;
}
