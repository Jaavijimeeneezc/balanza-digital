#pragma once

// Pinout de la ESP32-CAM de AI-Thinker (la de la foto: módulo con ranura
// microSD, conector IPEX para antena externa y flash en GPIO 4).
//
// Si montas otra placa (M5Stack, TTGO, XIAO ESP32S3 Sense...) hay que
// cambiar este bloque entero: los pines no son compatibles entre sí.

#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27

#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5

#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22

// LED blanco de alta potencia. Ojo: calienta y deslumbra el display si lo
// dejas fijo; úsalo solo en pulsos cortos durante la captura.
#define FLASH_GPIO_NUM   4
