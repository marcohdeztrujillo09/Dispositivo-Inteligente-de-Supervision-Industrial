#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "neotimer.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include "config.h" // <-- Aquí importamos tus credenciales ocultas

WiFiClient espClient;
PubSubClient client(espClient);

// Función para conectar WiFi
void conectarWiFi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
}

// Función para conectar MQTT
void conectarMQTT() {
  while (!client.connected()) {
    Serial.print("Intentando conexión MQTT...");
    if (client.connect("ESP32_Estacion")) {
      Serial.println("¡Conectado!");
    } else {
      Serial.print("Falló con estado: ");
      Serial.print(client.state());
      delay(2000);
    }
  }
}
// -------------------------------------------------

// --- DEFINICIONES SENSORES I2C ---
#define DIRECCION_BH1750 0x23     // Dirección I2C del sensor BH1750.
Adafruit_BME280 bme;               // Objeto para Temperatura, Humedad y Presión

// --- DEFINICIONES SENSORES ANALÓGICOS ---
int MQ135_PIN = 34;                // Pin para el sensor de gas
const int MAX9814_PIN = 35;        // Pin para el micrófono

int raw;                           // ADC MQ135
const int ventanaMuestreo = 5000;   // Duración de la ventana de medición en ms
const float referencia = 100.0;    // Valor de referencia para calcular dB relativos
float dbFiltrado = 0;              // Valor filtrado de dB
int valorMaximo = 0;                
int valorMinimo = 4095;
int lectura;             

Neotimer temporizador(ventanaMuestreo);

void setup() {
  Serial.begin(115200);
  
  // ----------- INICIO WIFI Y MQTT -----------
  conectarWiFi();
  client.setServer(mqtt_server, 1883);
  // ------------------------------------------

  // 1. Configuración de pines analógicos
  analogReadResolution(12);         // Resolución ADC 12 bits (0-4095)
  analogSetAttenuation(ADC_11db);   // Permite leer voltajes hasta ~3.3 V

  // 2. Iniciamos I2C en los pines del ESP32
  Wire.begin(21, 22); 
  delay(500); // Pausa para que los sensores se estabilicen

  // 3. Inicializar BME280 (Dirección 0x76)
  if (!bme.begin(0x76, &Wire)) {
    Serial.println("ALERTA: BME280 no detectado");
  }

  // 4. Inicializar BH1750 (Sensor de luz)
  Wire.beginTransmission(DIRECCION_BH1750);
  Wire.write(0x10); // Modo de alta resolución continua
  Wire.endTransmission();

  Serial.println("Sistema Unificado Listo...");
}

void loop() {
  if (!client.connected()) {
    conectarMQTT();
  }
  client.loop();

  // --- LECTURA SENSORES ANALÓGICOS (MICRÓFONO Y GAS) ---
  lectura = analogRead(MAX9814_PIN);
  raw = analogRead(MQ135_PIN);

  if (lectura > valorMaximo) valorMaximo = lectura;  
  if (lectura < valorMinimo) valorMinimo = lectura;

  // --- CÁLCULO DE dB Y ENVÍO DE DATOS (Cada 500ms) ---
  if (temporizador.repeat()) {
    // Cálculo de Sonido
    int picoAPico = valorMaximo - valorMinimo;
    if (picoAPico < 20) picoAPico = 20; 
    float dbInstantaneo = 20.0 * log10(picoAPico / referencia);
    dbFiltrado = 0.7 * dbFiltrado + 0.3 * dbInstantaneo;

    // --- LECTURA DE LUZ (BH1750) ---
    float luxes = 0;
    if (Wire.requestFrom(DIRECCION_BH1750, 2) == 2) {
      uint16_t lecturaLuz = (Wire.read() << 8) | Wire.read();
      luxes = lecturaLuz / 1.2;
    }

    // --- LECTURA AMBIENTAL (BME280) ---
    float temperatura = bme.readTemperature();
    float humedad = bme.readHumidity();
    float presion = bme.readPressure() / 100.0F; // Se divide por 100 para pasarlo a hPa

    // ----------- CREACIÓN JSON PARA MQTT -----------
    String payload = "{";
    payload += "\"temp\":" + String(temperatura,1) + ",";
    payload += "\"hum\":" + String(humedad,0) + ",";
    payload += "\"pres\":" + String(presion,1) + ",";
    payload += "\"lux\":" + String(luxes,0) + ",";
    payload += "\"db\":" + String(dbFiltrado,1) + ",";
    payload += "\"gas_adc\":" + String(raw);
    payload += "}";

    client.publish("estacion/datos", payload.c_str());
    // -----------------------------------------------

    // --- IMPRESIÓN UNIFICADA EN MONITOR SERIE ---
    Serial.print("TEMP: "); Serial.print(temperatura, 1); Serial.print("C | ");
    Serial.print("HUM: "); Serial.print(humedad, 0); Serial.print("% | ");
    Serial.print("PRES: "); Serial.print(presion, 1); Serial.print(" hPa | ");
    Serial.print("LUZ: "); Serial.print(luxes, 0); Serial.print(" lx | ");
    Serial.print("dB: "); Serial.print(dbFiltrado, 1); Serial.print(" | ");
    Serial.print("GAS(ADC): "); Serial.println(raw);

    // Reiniciar valores para la siguiente ventana de sonido
    valorMaximo = 0;
    valorMinimo = 4095;
  }
}