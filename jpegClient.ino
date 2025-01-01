#pragma GCC push_options
#pragma GCC optimize ("O3") // O3 boosts fps by 20%

#include "JPEGDEC.h"
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

TFT_eSPI tft = TFT_eSPI();
WiFiClient client;
JPEGDEC jpeg;
const char* SSID = "";
const char* PASSWORD = "";
const int PORT = 5451;
const char* HOST = "192.168.1.120"; // host ip address 
const int bufferSize = 50000; // buffer can be smaller as each frame is only around 3.5kb at 50 jpeg quality
uint8_t *buffer;
int bufferLength = 0;
const byte requestMessage[] = {0x55, 0x44, 0x55, 0x11}; // request message should probably be longer to avoid a false positive
unsigned long lastUpdate = 0;
int updates = 0;

int drawJpeg(JPEGDRAW *pDraw) {
  tft.pushImage(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight, pDraw->pPixels); // Используем стандартный метод вывода изображения
  return 1;
}

void setup() {
  buffer = (uint8_t*)malloc(bufferSize * sizeof(uint8_t));
  Serial.begin(115200); // увеличиваем скорость передачи данных для уменьшения задержек
  
  tft.init();
  tft.setRotation(3);
  tft.setTextSize(2);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN);
  tft.println("Connecting to Wi-Fi");
  tft.setTextColor(TFT_WHITE);
  tft.println(SSID);
  WiFi.begin(SSID, PASSWORD); // TODO: allow device to reconnect if connection is lost

  while (WiFi.status() != WL_CONNECTED) { }
  
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN);
  tft.setCursor(0, 0);
  tft.setTextSize(2);
  tft.println("Wi-Fi connected");

  if(client.connect(HOST, PORT)) {
    tft.println("TCP connected");
    client.write(requestMessage, sizeof(requestMessage)); // отправляем запрос на изображение сразу после подключения
  } else {
    tft.setTextColor(TFT_RED);
    tft.println("TCP FAILED");
  }

  lastUpdate = millis();

  // Создаем задачи для обработки данных и декодирования JPEG на разных ядрах
  xTaskCreatePinnedToCore(dataTask, "Data Task", 8192, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(decodeTask, "Decode Task", 8192, NULL, 1, NULL, 1);
}

void loop() {
  // Пустая функция loop() для устранения ошибки
}

void dataTask(void *pvParameters) {
  while (true) {
    int dataAvailable = client.available();

    if(dataAvailable > 0) {
      int bytesRead = client.read(&buffer[bufferLength], dataAvailable);
      bufferLength += bytesRead;
      Serial.print("Data received: ");
      Serial.println(bytesRead);
    }

    vTaskDelay(1); // Добавляем задержку для предотвращения перегрузки процессора
  }
}

void decodeTask(void *pvParameters) {
  while (true) {
    for(int i = bufferLength; i > 0; --i) { // count backwards from data buffer to search for footer
      if(buffer[i - 3] == 0x55 && buffer[i - 2] == 0x44 && buffer[i - 1] == 0x55 && buffer[i] == 0x11) {
        int dataLength = i - 4;
        bufferLength = 0;

        client.write(requestMessage, sizeof(requestMessage)); // queue up a new frame before decoding the current one

        if(jpeg.openRAM(buffer, dataLength, drawJpeg)) {
          jpeg.setPixelType(RGB565_BIG_ENDIAN);

          if(!jpeg.decode(0, 0, 1)) {
            Serial.println("Could not decode jpeg");
            jpeg.close();
          }
          jpeg.close();
        } else {
          Serial.println("Could not open jpeg");
          jpeg.close();
        }

        updates += 1;
        break;
      }
    }

    unsigned long time = millis();
    
    if(time > lastUpdate + 1000) {
      lastUpdate = time;
      Serial.print("FPS: ");
      Serial.println(updates);
      updates = 0;
    }

    vTaskDelay(1); // Добавляем задержку для предотвращения перегрузки процессора
  }
}

#pragma GCC pop_options
