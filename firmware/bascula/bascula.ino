#include <WiFi.h>
#include <WiFiClient.h>
#include "esp_camera.h"

// ================= DATOS A RELLENAR =================
const char* ssid = "";         // <-- TU WIFI
const char* password = "";    // <-- TU CLAVE

// IP de tu PC (Donde corre el Python)
const char* ftp_server = ""; // <-- ¡REVISA TU IP CON ipconfig!
const char* ftp_user = "";
const char* ftp_pass = "";
const int ftp_port = 21;
// ====================================================

// Configuración de pines para AI Thinker ESP32-CAM
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM       5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

WiFiClient client; // Cliente para comandos (Puerto 21)

// Declaramos la función antes para evitar errores
String leerRespuesta();

void setup() {
  Serial.begin(115200);
  
  // Configuración de la cámara
  camera_config_t config;
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  if(psramFound()){
    config.frame_size = FRAMESIZE_VGA; // Calidad media (640x480)
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // Iniciar cámara
  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Error al iniciar la cámara");
    return;
  }

  // Conectar a WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi Conectado!");
}

void loop() {
  // 1. Tomar Foto
  Serial.println("---------------------------");
  Serial.println("Tomando foto...");
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Fallo al capturar imagen");
    delay(5000);
    return;
  }

  // 2. Conectar al Servidor (Canal de Control)
  if (client.connect(ftp_server, ftp_port)) {
    Serial.println("Conectado al servidor FTP!");
    leerRespuesta(); // Saludo del servidor

    // Login
    client.println("USER " + String(ftp_user));
    leerRespuesta();
    client.println("PASS " + String(ftp_pass));
    leerRespuesta();

    // 3. ACTIVAR MODO PASIVO
    client.println("PASV");
    String respuesta = leerRespuesta(); 
    
    // Buscar paréntesis y calcular puerto
    int inicio = respuesta.indexOf('(');
    int fin = respuesta.indexOf(')');
    
    if (inicio != -1 && fin != -1) {
      String datos = respuesta.substring(inicio + 1, fin);
      
      int coma5 = datos.lastIndexOf(',');
      int p2 = datos.substring(coma5 + 1).toInt(); 
      
      String resto = datos.substring(0, coma5);
      int coma4 = resto.lastIndexOf(',');
      int p1 = resto.substring(coma4 + 1).toInt(); 
      
      // Fórmula FTP: (p1 * 256) + p2
      int puertoDatos = (p1 * 256) + p2;
      Serial.print("Puerto de datos calculado: ");
      Serial.println(puertoDatos);

      // 4. Conectar al Canal de Datos
      WiFiClient dataClient;
      if (dataClient.connect(ftp_server, puertoDatos)) {
        Serial.println("Canal de datos abierto CORRECTAMENTE!");
        
        // Avisar subida
        String nombreArchivo = "foto_" + String(millis()) + ".jpg";
        client.println("STOR " + nombreArchivo);
        leerRespuesta();

        // 5. ENVIAR LA FOTO
        size_t totalBytes = fb->len;
        size_t sentBytes = 0;
        const size_t chunkSize = 1024;
        uint8_t *buffer = fb->buf;

        while (sentBytes < totalBytes) {
            size_t bytesToRead = totalBytes - sentBytes;
            if (bytesToRead > chunkSize) bytesToRead = chunkSize;
            dataClient.write(buffer + sentBytes, bytesToRead);
            sentBytes += bytesToRead;
        }
        
        dataClient.stop(); // Cerrar canal de datos
        Serial.println("Foto subida: " + nombreArchivo);
        
        leerRespuesta(); // Respuesta final
      } else {
        Serial.println("Error: No se pudo conectar al puerto de datos (Firewall?)");
      }
    } else {
      Serial.println("Error: El servidor no respondió bien al comando PASV");
    }
    
    client.println("QUIT");
    client.stop();
    Serial.println("Desconectado.");
  } else {
    Serial.println("Error al conectar con el servidor FTP (Revisa IP).");
  }

  esp_camera_fb_return(fb); // Liberar memoria
  
  // Esperar 10 segundos antes de la siguiente foto
  delay(10000); 
}

// Función auxiliar para leer respuesta del servidor
String leerRespuesta() {
  String resp = "";
  unsigned long tiempo = millis();
  while (client.connected() && !client.available() && millis() - tiempo < 5000) {
    delay(1); 
  }
  while (client.available()) {
    char c = client.read();
    resp += c;
  }
  // Descomenta si quieres ver todo el chat en el monitor
  // Serial.print("S: "); Serial.println(resp); 
  return resp;
}