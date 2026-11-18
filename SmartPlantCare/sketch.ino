/*
 * Smart Plant Care - ESP32 (VERSI WOKWI)
 * ------------------------------------------------------------
 * Sensor  : Kelembapan tanah (analog), Sensor hujan (analog), DHT22
 * Output  : Servo (katup penyiram), Serial Monitor, ThingSpeak (WiFi)
 *
 * Library yang dibutuhkan (Library Manager):
 *  - DHT sensor library (Adafruit) + Adafruit Unified Sensor
 *  - ESP32Servo (Kevin Harrington)
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <ESP32Servo.h>

// ===================== KONFIGURASI =====================
// WiFi & ThingSpeak 
const char* WIFI_SSID          = "Wokwi-GUEST";   // WiFi simulasi Wokwi
const char* WIFI_PASS          = "";              // tanpa password
const char* THINGSPEAK_API_KEY = "WI05S6###########";

// Pin (gunakan ADC1 untuk analog karena ADC2 bentrok dengan WiFi)
const int PIN_SOIL  = 34;   // AO sensor kelembapan tanah
const int PIN_RAIN  = 35;   // AO sensor hujan
const int PIN_DHT   = 4;    // DATA DHT22
const int PIN_SERVO = 18;   // Signal servo

// Kalibrasi untuk simulasi (potensiometer 0-4095). Untuk sensor asli, pakai nilai hasil kalibrasi
const int SOIL_DRY = 4095;  // potensiometer di posisi maksimum = tanah kering
const int SOIL_WET = 0;     // potensiometer di posisi minimum = tanah basah

// Ambang batas logika
const int SOIL_THRESHOLD_ON  = 30;   // % -> di bawah ini dianggap kering, mulai siram
const int SOIL_THRESHOLD_OFF = 50;   // % -> di atas ini dianggap cukup basah, stop siram
const int RAIN_THRESHOLD     = 2000; // nilai ADC di bawah ini = hujan

// Posisi servo
const int SERVO_OPEN  = 90;  // katup terbuka (menyiram)
const int SERVO_CLOSE = 0;   // katup tertutup

// Interval waktu (ms)
const unsigned long READ_INTERVAL   = 2000;   // baca sensor tiap 2 detik
const unsigned long UPLOAD_INTERVAL = 20000;  // kirim ke ThingSpeak tiap 20 detik

// ===================== OBJEK & VARIABEL =====================
DHT dht(PIN_DHT, DHT22);
Servo valveServo;

int   soilPercent = 0;
bool  isRaining   = false;
bool  valveOpen   = false;
float temperature = 0;
float humidity    = 0;

unsigned long lastRead   = 0;
unsigned long lastUpload = 0;

// ===================== FUNGSI =====================

// Menyambungkan ke WiFi (dipanggil saat setup dan saat koneksi putus)
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("Menghubungkan ke WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\nWiFi terhubung, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nGagal terhubung WiFi, akan dicoba lagi...");
  }
}

// Membaca semua sensor dan mengubahnya ke satuan yang mudah dibaca
void readSensors() {
  // Sensor tanah: konversi ADC ke persen (0% kering, 100% basah)
  int soilRaw = analogRead(PIN_SOIL);
  soilPercent = map(soilRaw, SOIL_DRY, SOIL_WET, 0, 100);
  soilPercent = constrain(soilPercent, 0, 100);

  // Sensor hujan: nilai ADC kecil berarti permukaan basah
  int rainRaw = analogRead(PIN_RAIN);
  isRaining = (rainRaw < RAIN_THRESHOLD);

  // DHT22: abaikan data jika pembacaan gagal (NaN)
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t) && !isnan(h)) {
    temperature = t;
    humidity    = h;
  } else {
    Serial.println("Peringatan: gagal membaca DHT22");
  }
}

// Logika kontrol servo dengan histeresis agar servo tidak bolak-balik
void controlValve() {
  if (!valveOpen && soilPercent < SOIL_THRESHOLD_ON && !isRaining) {
    valveServo.write(SERVO_OPEN);
    valveOpen = true;
  } else if (valveOpen && (soilPercent > SOIL_THRESHOLD_OFF || isRaining)) {
    valveServo.write(SERVO_CLOSE);
    valveOpen = false;
  }
}

// Menampilkan data ke Serial Monitor
void printStatus() {
  Serial.printf("Tanah: %d%% | Hujan: %s | Suhu: %.1f C | Lembap: %.1f%% | Servo: %s | WiFi: %s\n",
                soilPercent,
                isRaining ? "Ya" : "Tidak",
                temperature,
                humidity,
                valveOpen ? "BUKA" : "TUTUP",
                WiFi.status() == WL_CONNECTED ? "OK" : "PUTUS");
}

// Mengirim data ke ThingSpeak lewat HTTP GET
void uploadToThingSpeak() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = "http://api.thingspeak.com/update?api_key=" + String(THINGSPEAK_API_KEY) +
               "&field1=" + String(soilPercent) +
               "&field2=" + String(temperature, 1) +
               "&field3=" + String(humidity, 1) +
               "&field4=" + String(isRaining ? 1 : 0) +
               "&field5=" + String(valveOpen ? 1 : 0);

  http.begin(url);
  int code = http.GET();
  if (code > 0) {
    Serial.printf("ThingSpeak: HTTP %d (entry ke-%s)\n", code, http.getString().c_str());
  } else {
    Serial.printf("ThingSpeak gagal: %s\n", http.errorToString(code).c_str());
  }
  http.end();
}

// ===================== SETUP & LOOP =====================
void setup() {
  Serial.begin(115200);
  dht.begin();

  valveServo.attach(PIN_SERVO);
  valveServo.write(SERVO_CLOSE);   // posisi awal: tertutup

  connectWiFi();
  Serial.println("Sistem Smart Plant Care siap.");
}

void loop() {
  unsigned long now = millis();

  // Pastikan WiFi tetap terhubung (reconnect otomatis)
  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  // Baca sensor, kontrol servo, tampilkan status
  if (now - lastRead >= READ_INTERVAL) {
    lastRead = now;
    readSensors();
    controlValve();
    printStatus();
  }

  // Kirim data ke cloud
  if (now - lastUpload >= UPLOAD_INTERVAL) {
    lastUpload = now;
    uploadToThingSpeak();
  }
}
