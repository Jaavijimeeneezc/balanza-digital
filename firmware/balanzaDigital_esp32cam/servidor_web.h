#pragma once

#include <esp_err.h>

// Levanta los dos servidores HTTP:
//   :80  → /capture, /status, /health   (control, respuesta rápida)
//   :81  → /stream                       (MJPEG continuo)
//
// Van separados a propósito. El stream MJPEG ocupa su conexión de forma
// indefinida y, con un solo servidor, una petición de captura se quedaría
// esperando a que el navegador cerrase el vídeo.
esp_err_t iniciarServidorWeb();

// Cuenta de fotogramas servidos, para el JSON de /status.
uint32_t fotogramasServidos();
