# Balanza digital

Lectura automática de básculas analógicas mediante visión por computador. Una
placa ESP32-CAM apunta al display de la báscula y transmite la imagen; el
reconocimiento del peso se hace en el servidor.

## Arquitectura

La placa no calcula el peso. Solo aporta imagen:

1. Se conecta al WiFi y al broker MQTT.
2. Se anuncia en `bascula/{id}/estado` con un mensaje retenido. Ese anuncio
   es todo el descubrimiento: el backend la da de alta sin configurar
   ninguna IP a mano.
3. Sirve video en el puerto 81 y captura puntual en el 80.
4. Escucha órdenes en `bascula/{id}/comando`.

El OCR vive en el backend, que tiene CPU de sobra y se puede afinar sin
reflashear las placas desplegadas.

## Contenido

- `firmware/balanzaDigital_esp32cam/` — firmware principal (MQTT, cámara,
  servidor HTTP).
- `firmware/bascula/` — versión anterior, más simple, que subía las capturas
  por FTP.
- `modelos_3d/` — soporte imprimible en 3D: base, brazo, pie y tapa (STL).

## Dependencias

ArduinoJson, PubSubClient, esp32-camera.

## Configuración

Copia `secrets.h.ejemplo` a `secrets.h` y rellena los valores. `secrets.h`
está en `.gitignore` y no se publica.

## Licencia

MIT. Ver `LICENSE`.
