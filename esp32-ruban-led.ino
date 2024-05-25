#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>

#define BLUE1 2
#define GREEN1 4
#define RED1 5

#define BLUE2 12 
#define GREEN2 13
#define RED2 14 

int mode = 1; // Variable pour stocker le mode d'utilisation du ruban LED (0: normal, 1: music)
int OnOff = 1;
int color[] = {255, 255, 255, 255, 255, 255};

class LedController {
public:
    LedController() {
        pinMode(RED1, OUTPUT);
        pinMode(GREEN1, OUTPUT);
        pinMode(BLUE1, OUTPUT);

        pinMode(RED2, OUTPUT);
        pinMode(GREEN2, OUTPUT);
        pinMode(BLUE2, OUTPUT);
    }

    struct RGB {
        int r;
        int g;
        int b;
    };

    void setColor(int led, int red, int green, int blue) {
        if (led == 0) {
            analogWrite(RED1, red);
            analogWrite(GREEN1, green);
            analogWrite(BLUE1, blue);
        } else if (led == 1) {
            analogWrite(RED2, red);
            analogWrite(GREEN2, green);
            analogWrite(BLUE2, blue);
        } else {
            Serial.println("Erreur: aucune LED trouvée");
        }
    }

    float calculateBassIntensity(const float* audioArray, int size) {
        float bass = 0.0;
        int bassCount = min(40, size); // Assurez-vous de ne pas dépasser les limites du tableau
        for (int j = 0; j < bassCount; j++) {
            bass += audioArray[j];
        }
        bass = bass * 2 / (bassCount * 2 * 0.1); // Normalisation
        float audioIntensity = min(1.0, max(0.1, (round(bass * 0.5 * 5) * 10) / 100));
        return audioIntensity * 25;
    }

    RGB decimalToRgb(int receivedColor) {
      int r = (receivedColor >> 16) & 255;
      int g = (receivedColor >> 8) & 255;
      int b = receivedColor & 255;
      return {r, g, b};
    }
    
    void background(float *audioArray, int audioArraySize, int receivedColor) {
        if (audioArray && audioArraySize > 0 && receivedColor) {
            RGB rgb = decimalToRgb(receivedColor);
            float rawIntensity = calculateBassIntensity(audioArray, audioArraySize);

            float threshold = 0.5; // Seuil de détection de battement à ajuster
            float significantThreshold = 2.3; // Seuil pour forcer le clignotement
            int intensity = 0;
    
            // Vérification du dépassement du seuil significatif
            for (int i = 0; i < audioArraySize; i++) {
                if (audioArray[i] > significantThreshold) {
                    intensity = 100; // Intensité maximale pour clignotement rapide
                    break;
                } else if (audioArray[i] > 0.01 && audioArray[i] < threshold) {
                    intensity = min(25, (int)(rawIntensity * 25));
                } else {
                    intensity = 0;
                }
            }
    
            int adjustedColors[6] = {
                rgb.r * intensity / 100, 
                rgb.g * intensity / 100, 
                rgb.b * intensity / 100, 
                rgb.r * intensity / 100, 
                rgb.g * intensity / 100, 
                rgb.b * intensity / 100
            };

            setColor(0, adjustedColors[0], adjustedColors[1], adjustedColors[2]);
            setColor(1, adjustedColors[3], adjustedColors[4], adjustedColors[5]);
        }
    }
};

class WebSocket {
private:
    WebSocketsServer *ws;
    LedController *ledController;

public:
    WebSocket(WebSocketsServer *ws, LedController *ledController) {
        this->ws = ws;
        this->ledController = ledController;
    }

    void begin() {
        this->ws->onEvent(std::bind(&WebSocket::handleEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
    }

    void handleEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
        if (type == WStype_TEXT) {
            StaticJsonDocument<200> doc;
            deserializeJson(doc, payload, length);
            if (doc.containsKey("type")) {
                String messageType = doc["type"];
                if (messageType == "setcolor") {
                    String hexColor = doc["data"];
                    long hexValue = strtol(hexColor.c_str(), NULL, 16);
                    int red = (hexValue >> 16) & 0xFF;
                    int green = (hexValue >> 8) & 0xFF;
                    int blue = hexValue & 0xFF;
                    color[0] = red;
                    color[1] = green;
                    color[2] = blue;
    
                    color[3] = 255 - red;
                    color[4] = 255 - green;
                    color[5] = 255 - blue;
                    if (mode == 0 && OnOff == 1) {
                        ledController->setColor(0, color[0], color[1], color[2]);
                        ledController->setColor(1, color[3], color[4], color[5]);
                    }
                } else if (messageType == "setmode") {
                    String newMode = doc["data"];
                    if (newMode == "normal") {
                        mode = 0;
                        if (OnOff == 1) {
                            ledController->setColor(0, color[0], color[1], color[2]);
                            ledController->setColor(1, color[3], color[4], color[5]);
                        }
                    } else if (newMode == "music") {
                        mode = 1;
                        if (OnOff == 1) {
                            ledController->setColor(0, 0, 0, 0);
                            ledController->setColor(1, 0, 0, 0);
                        }
                    }
                } else if (messageType == "setonoff") {
                    String newOnOff = doc["data"];
                    if (newOnOff == "on") {
                        OnOff = 1;
                        if (mode == 0) {
                            ledController->setColor(0, color[0], color[1], color[2]);
                            ledController->setColor(1, color[3], color[4], color[5]);
                        }
                    } else if (newOnOff == "off") {
                        OnOff = 0;
                        ledController->setColor(0, 0, 0, 0);
                        ledController->setColor(1, 0, 0, 0);
                    }
                }
            }
        }
    }
};

class WebSocketClient {
public:
    WebSocketClient(WebSocketsClient &ws, LedController &controller) : webSocket(ws), ledController(controller) {}

    void begin(const char* host, int port) {
        webSocket.begin(host, port, "/");
        webSocket.onEvent(std::bind(&WebSocketClient::handleEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
                Serial.println("starting websocket client");

    }

    void loop() {
        webSocket.loop();
    }

    void handleEvent(WStype_t type, uint8_t *payload, size_t length) {
      switch (type) {
        case WStype_TEXT:
        Serial.println("receive");
          if (mode == 1) { // Mode music
            String payloadString = String((char *)payload);
    
            DynamicJsonDocument doc(1024);
            deserializeJson(doc, payloadString);
    
            JsonObject data = doc["data"];
            JsonArray audioArrayJson = data["audioArray"];
            int audioArraySize = audioArrayJson.size();
            float audioArray[audioArraySize];
            for (int i = 0; i < audioArraySize; i++) {
              audioArray[i] = audioArrayJson[i];
            }
    
            int backgroundColor = data["backgroundColor"];
    
            ledController.background(audioArray, audioArraySize, backgroundColor);
          }
          break;
      }
    }


private:
    LedController &ledController;
    WebSocketsClient &webSocket;
};

class CustomWebServer {
public:
    CustomWebServer(WebServer &server) : webServer(server) {}

    void begin() {
        webServer.on("/", HTTP_GET, std::bind(&CustomWebServer::handleRoot, this));
        webServer.begin();
    }

    void handleRoot() {
        HTTPClient http;
        http.begin("https://raw.githubusercontent.com/tariteur/ruban-led/main/page.html");
        int httpCode = http.GET();
        String content = httpCode > 0 ? http.getString() : "Failed to fetch HTML.";
        http.end();

        webServer.send(200, "text/html", content);
    }

    void handleClient() {
        webServer.handleClient();
    }

private:
    WebServer &webServer;
};

LedController ledController;
WebServer server(80);
CustomWebServer customWebServer(server);
WebSocketsServer webSocketServer(81);
WebSocketsClient webSocket;
WebSocket websocketServerHandler(&webSocketServer, &ledController);
WebSocketClient webSocketClient(webSocket, ledController);

void setup() {
    Serial.begin(115200);
    delay(1000);

    WiFi.begin("default", "");
    //WiFi.begin("Livebox-2148", "E95473C131474D51FD234EEA5E");
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    ledController.setColor(0, 0, 0, 0);
    ledController.setColor(1, 0, 0, 0);

    customWebServer.begin();
    webSocketServer.begin();
    websocketServerHandler.begin();
    webSocketClient.begin("192.168.1.46", 80);

    Serial.println("Connecté au réseau WiFi.");
}

void loop() {
    customWebServer.handleClient();
    webSocketServer.loop();
    webSocketClient.loop();
}
