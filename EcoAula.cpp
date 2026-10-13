/*
 * ============================================================
 *  EcoAula Inteligente  -  v1.0  (Etapa 1)
 *  ESP32-C3 Super Mini (OLED integrada) + DHT11 + Firebase RTDB
 * ============================================================
 *
 *  LIBRERIAS (Arduino IDE -> Herramientas -> Administrar bibliotecas):
 *    1) U8g2                    (de oliver)
 *    2) DHT sensor library      (de Adafruit)
 *    3) Adafruit Unified Sensor (de Adafruit, la pide la anterior)
 *  Todo lo demas (WiFi, WebServer, DNSServer, Preferences, HTTPClient)
 *  ya viene con el core "esp32" de Espressif.
 *
 *  CONFIGURACION DE LA PLACA (Herramientas):
 *    Placa ............... ESP32C3 Dev Module
 *    USB CDC On Boot ..... Enabled   (para ver el monitor serial)
 *    Flash Size .......... 4MB
 *    Upload Speed ........ 115200 (si falla la carga, bajar velocidad)
 *
 *  PINES:
 *    GPIO0  -> Boton (al otro lado a GND)
 *    GPIO1  -> LED de estado (con resistencia 220-330 ohm a GND)
 *    GPIO3  -> DATA del DHT11
 *    GPIO5  -> SDA OLED  (ya conectado en la placa)
 *    GPIO6  -> SCL OLED  (ya conectado en la placa)
 *
 *  USO DEL BOTON:
 *    Toque corto  = siguiente opcion / volver
 *    Mantener 0.8s = entrar / confirmar
 *    Mantener el boton al ENCENDER = forzar modo configuracion
 *
 *  LED:
 *    Parpadeo rapido  = modo configuracion
 *    Apagado          = sin WiFi
 *    Parpadeo lento   = WiFi OK pero Firebase aun sin confirmar / con error
 *    Encendido fijo   = WiFi OK y Firebase recibiendo datos
 * ============================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <U8g2lib.h>
#include <DHT.h>

// ===================== PINES =====================
#define PIN_LED 1
#define PIN_BTN 0
#define PIN_DHT 3
#define PIN_SDA 5
#define PIN_SCL 6
#define DHTTYPE DHT11

// ===================== PANTALLA ==================
// 1 = OLED integrada 0.42" (72x40) de la placa ESP32-C3 con pantalla
// 0 = OLED externa 0.96" (128x64) en los mismos pines SDA/SCL
#define PANTALLA_042 1

#if PANTALLA_042
  U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);
  // Si la pantalla sale en blanco, prueba esta linea en su lugar (I2C por software):
  // U8G2_SSD1306_72X40_ER_F_SW_I2C u8g2(U8G2_R0, PIN_SCL, PIN_SDA, U8X8_PIN_NONE);
  const int ANCHO = 72, ALTO = 40;
#else
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);
  const int ANCHO = 128, ALTO = 64;
#endif
const int MAXCH = ANCHO / 5;   // caracteres que caben por fila (fuente 5x7)

// ===================== AJUSTES ===================
const char*    AP_SSID           = "EcoAula-Setup";
const char*    AP_PASS           = "ecoaula123";   // minimo 8 caracteres
const uint32_t LONG_MS           = 800;     // pulsacion larga
const uint32_t DHT_INTERVALO     = 2500;    // lectura del sensor
const uint32_t FB_INTERVALO_OK   = 60000;   // envio a Firebase (cada 60 s)
const uint32_t FB_INTERVALO_ERR  = 15000;   // reintento si fallo
const bool     GUARDAR_HISTORIAL = true;    // ademas de "actual", guardar historial

// ===================== OBJETOS ===================
DHT         dht(PIN_DHT, DHTTYPE);
Preferences prefs;
WebServer   server(80);
DNSServer   dns;

// ===================== ESTADO ====================
enum Pantalla { P_MENU, P_WIFI, P_DHT, P_FIREBASE, P_CONFIRMAR, P_CONFIG };
Pantalla pantalla = P_MENU;

const int   MENU_N = 4;
const char* MENU_TXT[MENU_N] = { "WiFi", "Sensor DHT", "Firebase", "Reconfigurar" };
int  menuSel = 0;

bool modoConfig = false;

// Configuracion guardada
String cfgSsid, cfgPass, fbUrl, fbAuth, aulaId;

// Sensor
float    temp = NAN, hum = NAN;
bool     dhtOk = false;
uint32_t ultimaLecturaDHT = 0;

// Firebase
int      fbCodigo = 0;          // 0 = aun no intentado, 200 = OK, <0 error red, otro = HTTP
uint32_t ultimoEnvioOk = 0;
uint32_t proximoEnvio = 0;
bool     huboEnvioOk = false;

String listaRedesHTML;

// ===================== UTILIDADES ================
String esc(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  return s;
}

String normalizarUrl(String u) {
  u.trim();
  if (u.length() == 0) return u;
  if (!u.startsWith("http")) u = "https://" + u;
  while (u.endsWith("/")) u.remove(u.length() - 1);
  return u;
}

// Firebase no admite  . $ # [ ] /  en las claves
String limpiarAula(String a) {
  a.trim();
  String r = "";
  for (unsigned int i = 0; i < a.length(); i++) {
    char c = a[i];
    if (c == '.' || c == '$' || c == '#' || c == '[' || c == ']' || c == '/' || c == ' ') c = '_';
    r += c;
  }
  if (r.length() == 0) r = "aula01";
  return r;
}

void cargarConfig() {
  prefs.begin("ecoaula", true);
  cfgSsid = prefs.getString("ssid", "");
  cfgPass = prefs.getString("pass", "");
  fbUrl   = prefs.getString("url", "");
  fbAuth  = prefs.getString("auth", "");
  aulaId  = prefs.getString("aula", "aula01");
  prefs.end();
}

// ===================== PANTALLA ==================
void fila(int r, const String& s) {
  String t = s;
  if ((int)t.length() > MAXCH) t = t.substring(0, MAXCH);
  u8g2.drawStr(0, r * 8 + 7, t.c_str());
}

void mensaje(const String& a, const String& b = "", const String& c = "") {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tr);
  fila(1, a);
  fila(2, b);
  fila(3, c);
  u8g2.sendBuffer();
}

String hace(uint32_t desde) {
  uint32_t s = (millis() - desde) / 1000;
  if (s >= 60) return String(s / 60) + " min";
  return String(s) + " s";
}

void dibujar() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tr);

  switch (pantalla) {

    case P_MENU:
      fila(0, "-- MENU --");
      for (int i = 0; i < MENU_N; i++) {
        fila(i + 1, String(i == menuSel ? ">" : " ") + MENU_TXT[i]);
      }
      break;

    case P_WIFI:
      fila(0, "[WiFi]");
      if (WiFi.status() == WL_CONNECTED) {
        fila(1, WiFi.SSID());
        fila(2, WiFi.localIP().toString());
        fila(3, "RSSI " + String(WiFi.RSSI()) + "dBm");
      } else {
        fila(1, "Sin conexion");
        fila(2, "Red:");
        fila(3, cfgSsid);
      }
      fila(4, "Toca: volver");
      break;

    case P_DHT:
      fila(0, "[DHT11]");
      if (dhtOk) {
        fila(1, "Temp: " + String(temp, 1) + " C");
        fila(2, "Hum:  " + String(hum, 0) + " %");
        fila(3, "Lectura OK");
      } else {
        fila(1, "Temp: --");
        fila(2, "Hum:  --");
        fila(3, "ERROR sensor");
      }
      fila(4, "Toca: volver");
      break;

    case P_FIREBASE: {
      fila(0, "[Firebase]");
      fila(1, "Aula:" + aulaId);
      String est;
      if (fbUrl.length() == 0)                 est = "Sin URL";
      else if (WiFi.status() != WL_CONNECTED)  est = "Sin WiFi";
      else if (fbCodigo == 0)                  est = "Esperando...";
      else if (fbCodigo == 200)                est = "Conectado OK";
      else if (fbCodigo < 0)                   est = "Error red " + String(fbCodigo);
      else                                     est = "Error HTTP " + String(fbCodigo);
      fila(2, est);
      fila(3, huboEnvioOk ? "Ult: " + hace(ultimoEnvioOk) : "Ult: nunca");
      fila(4, "Toca: volver");
      break;
    }

    case P_CONFIRMAR:
      fila(0, "Reiniciar y");
      fila(1, "reconfigurar?");
      fila(3, "Toca: NO");
      fila(4, "Mant: SI");
      break;

    case P_CONFIG:
      fila(0, "MODO CONFIG");
      fila(1, AP_SSID);
      fila(2, "Clave:");
      fila(3, AP_PASS);
      fila(4, "192.168.4.1");
      break;
  }
  u8g2.sendBuffer();
}

// ===================== BOTON =====================
// Devuelve 0 = nada, 1 = toque corto, 2 = pulsacion larga
int leerBoton() {
  static bool     prev = false;
  static bool     largoEnviado = false;
  static uint32_t t0 = 0, tCambio = 0;

  bool     p   = (digitalRead(PIN_BTN) == LOW);
  uint32_t now = millis();

  if (p && !prev) {
    if (now - tCambio < 50) return 0;       // anti-rebote
    prev = true; largoEnviado = false; t0 = now; tCambio = now;
    return 0;
  }
  if (p && prev && !largoEnviado && (now - t0 >= LONG_MS)) {
    largoEnviado = true;
    return 2;
  }
  if (!p && prev) {
    prev = false; tCambio = now;
    if (!largoEnviado && (now - t0 >= 30)) return 1;
  }
  return 0;
}

// ===================== PORTAL DE CONFIGURACION ===
void escanearRedes() {
  int n = WiFi.scanNetworks();
  listaRedesHTML = "";
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;
    listaRedesHTML += "<option value=\"" + esc(s) + "\">" + esc(s) + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
  }
  WiFi.scanDelete();
}

void paginaRaiz() {
  String h = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
               "<meta name='viewport' content='width=device-width,initial-scale=1'>"
               "<title>EcoAula</title><style>"
               "body{font-family:sans-serif;background:#0f2a1d;color:#e8f5e9;margin:0;padding:16px}"
               ".c{max-width:420px;margin:auto;background:#17402b;padding:18px;border-radius:12px}"
               "h2{margin-top:0}label{display:block;margin-top:12px;font-size:14px}"
               "input,select{width:100%;padding:10px;margin-top:4px;border-radius:6px;border:0;box-sizing:border-box;font-size:16px}"
               "button{width:100%;margin-top:18px;padding:12px;font-size:16px;border:0;border-radius:8px;background:#43a047;color:#fff}"
               "a{color:#a5d6a7}small{opacity:.7}"
               "</style></head><body><div class='c'><h2>EcoAula Inteligente</h2>"
               "<form method='POST' action='/save'>");
  h += "<label>Red WiFi</label><select name='ssid_sel'>" + listaRedesHTML + "</select>";
  h += "<label>...o escribe el nombre de la red</label><input name='ssid_man' placeholder='(opcional)'>";
  h += "<label>Contrase&ntilde;a WiFi</label><input name='pass' type='password'>";
  h += "<label>URL de Firebase Realtime Database</label>"
       "<input name='url' placeholder='https://xxxx-default-rtdb.firebaseio.com' value='" + esc(fbUrl) + "'>";
  h += "<label>ID del aula</label><input name='aula' value='" + esc(aulaId) + "'>";
  h += "<label>Secret / token de Firebase <small>(opcional)</small></label><input name='auth'>";
  h += F("<button type='submit'>Guardar y reiniciar</button></form>"
         "<p><a href='/scan'>Buscar redes de nuevo</a></p></div></body></html>");
  server.send(200, "text/html; charset=utf-8", h);
}

void paginaScan() {
  escanearRedes();
  server.sendHeader("Location", "http://192.168.4.1/");
  server.send(302, "text/plain", "");
}

void paginaGuardar() {
  String man = server.arg("ssid_man");
  man.trim();
  String nuevoSsid = man.length() ? man : server.arg("ssid_sel");
  String nuevaPass = server.arg("pass");
  String url       = normalizarUrl(server.arg("url"));
  String aula      = limpiarAula(server.arg("aula"));
  String auth      = server.arg("auth");
  auth.trim();

  if (nuevoSsid.length() == 0 || url.length() == 0) {
    server.send(400, "text/html; charset=utf-8",
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<p>Faltan datos (red WiFi y URL de Firebase). <a href='/'>Volver</a></p>");
    return;
  }
  if (nuevaPass.length() == 0 && nuevoSsid == cfgSsid) nuevaPass = cfgPass;  // conservar clave anterior
  if (auth.length() == 0) auth = fbAuth;                                     // conservar token anterior

  prefs.begin("ecoaula", false);
  prefs.putString("ssid", nuevoSsid);
  prefs.putString("pass", nuevaPass);
  prefs.putString("url",  url);
  prefs.putString("auth", auth);
  prefs.putString("aula", aula);
  prefs.end();

  server.send(200, "text/html; charset=utf-8",
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<body style='font-family:sans-serif;text-align:center;padding:30px'>"
              "<h2>Guardado</h2><p>El EcoAula se esta reiniciando y se conectara a tu WiFi.</p></body>");
  mensaje("Guardado!", "Reiniciando...");
  delay(1500);
  ESP.restart();
}

void paginaNoEncontrada() {   // portal cautivo: todo redirige a la pagina principal
  server.sendHeader("Location", "http://192.168.4.1/");
  server.send(302, "text/plain", "");
}

void iniciarConfig() {
  modoConfig = true;
  pantalla   = P_CONFIG;
  mensaje("Preparando", "modo config...");

  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(300);
  escanearRedes();                     // se escanea antes de que alguien se conecte

  dns.start(53, "*", WiFi.softAPIP());
  server.on("/", HTTP_GET, paginaRaiz);
  server.on("/scan", HTTP_GET, paginaScan);
  server.on("/save", HTTP_POST, paginaGuardar);
  server.onNotFound(paginaNoEncontrada);
  server.begin();

  Serial.println("[CONFIG] AP: " + String(AP_SSID) + "  IP: " + WiFi.softAPIP().toString());
}

// ===================== WIFI NORMAL ===============
void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
  WiFi.setTxPower(WIFI_POWER_8_5dBm);  // truco: estabiliza WiFi en muchos C3 Super Mini

  uint32_t t0 = millis();
  int puntos = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    String p = "";
    for (int i = 0; i < (puntos % 4); i++) p += ".";
    mensaje("Conectando", cfgSsid, p);
    digitalWrite(PIN_LED, (millis() / 150) % 2);
    puntos++;
    delay(300);
  }
  digitalWrite(PIN_LED, LOW);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] OK  IP: " + WiFi.localIP().toString());
    mensaje("WiFi OK", WiFi.localIP().toString());
  } else {
    Serial.println("[WiFi] No conecto (se seguira intentando en segundo plano)");
    mensaje("Sin WiFi", "Reintentando...");
  }
  delay(1200);
}

// ===================== SENSOR ====================
void leerDHT() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (isnan(h) || isnan(t)) {
    dhtOk = false;
    Serial.println("[DHT] Error de lectura");
  } else {
    temp = t; hum = h; dhtOk = true;
    ultimaLecturaDHT = millis();
    Serial.printf("[DHT] %.1f C  %.0f %%\n", temp, hum);
  }
}

// ===================== FIREBASE ==================
bool enviarFirebase() {
  if (fbUrl.length() == 0) { fbCodigo = -99; return false; }

  WiFiClientSecure cli;
  cli.setInsecure();   // sin validar certificado (simple para empezar)
  HTTPClient http;
  http.setTimeout(8000);
  http.setConnectTimeout(8000);

  String sufijo = fbAuth.length() ? "?auth=" + fbAuth : "";
  String json = "{\"temperatura\":" + String(temp, 1) +
                ",\"humedad\":" + String(hum, 0) +
                ",\"rssi\":" + String(WiFi.RSSI()) +
                ",\"uptime_s\":" + String(millis() / 1000) +
                ",\"ts\":{\".sv\":\"timestamp\"}}";

  // 1) Dato en vivo: /aulas/<aula>/actual
  String u1 = fbUrl + "/aulas/" + aulaId + "/actual.json" + sufijo;
  if (!http.begin(cli, u1)) { fbCodigo = -1; return false; }
  http.addHeader("Content-Type", "application/json");
  int c = http.PUT(json);
  http.end();
  fbCodigo = c;
  Serial.printf("[FB] actual -> %d\n", c);
  if (c != 200) return false;

  // 2) Historial: /aulas/<aula>/historial/<id automatico>
  if (GUARDAR_HISTORIAL) {
    String u2 = fbUrl + "/aulas/" + aulaId + "/historial.json" + sufijo;
    if (http.begin(cli, u2)) {
      http.addHeader("Content-Type", "application/json");
      int c2 = http.POST(json);
      http.end();
      Serial.printf("[FB] historial -> %d\n", c2);
    }
  }
  return true;
}

// ===================== LED =======================
void actualizarLed(uint32_t now) {
  if (modoConfig) { digitalWrite(PIN_LED, (now / 250) % 2); return; }
  if (WiFi.status() != WL_CONNECTED) { digitalWrite(PIN_LED, LOW); return; }
  if (fbCodigo == 200) digitalWrite(PIN_LED, HIGH);
  else                 digitalWrite(PIN_LED, (now / 1000) % 2);
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BTN, INPUT_PULLUP);
  digitalWrite(PIN_LED, LOW);

  u8g2.begin();
  dht.begin();
  cargarConfig();

  mensaje("EcoAula", "Inteligente", "v1.0");
  delay(1500);

  bool forzarConfig = (digitalRead(PIN_BTN) == LOW);   // boton presionado al encender

  if (forzarConfig || cfgSsid.length() == 0) {
    iniciarConfig();
  } else {
    conectarWiFi();
    pantalla = P_MENU;
    leerDHT();
  }
}

// ===================== LOOP ======================
void loop() {
  uint32_t now = millis();
  int b = leerBoton();
  static uint32_t tDibujo = 0;

  actualizarLed(now);

  // ---------- Modo configuracion ----------
  if (modoConfig) {
    dns.processNextRequest();
    server.handleClient();
    if (b == 2) ESP.restart();            // mantener boton = salir sin guardar
    if (now - tDibujo > 500) { tDibujo = now; dibujar(); }
    return;
  }

  // ---------- Modo normal ----------
  // Boton / menu
  if (b != 0) {
    switch (pantalla) {
      case P_MENU:
        if (b == 1) menuSel = (menuSel + 1) % MENU_N;
        else {
          if (menuSel == 0) pantalla = P_WIFI;
          if (menuSel == 1) pantalla = P_DHT;
          if (menuSel == 2) pantalla = P_FIREBASE;
          if (menuSel == 3) pantalla = P_CONFIRMAR;
        }
        break;
      case P_WIFI:
      case P_DHT:
      case P_FIREBASE:
        pantalla = P_MENU;                // cualquier pulsacion vuelve
        break;
      case P_CONFIRMAR:
        if (b == 1) pantalla = P_MENU;
        else        iniciarConfig();
        break;
      default: break;
    }
    tDibujo = 0;                          // redibujar ya
  }

  // Sensor
  static uint32_t tDHT = 0;
  if (now - tDHT >= DHT_INTERVALO) { tDHT = now; leerDHT(); }

  // Reconexion WiFi
  static uint32_t tWifi = 0;
  if (WiFi.status() != WL_CONNECTED && now - tWifi > 10000) {
    tWifi = now;
    Serial.println("[WiFi] Reintentando...");
    WiFi.reconnect();
  }

  // Firebase
  if (WiFi.status() == WL_CONNECTED && dhtOk && (int32_t)(now - proximoEnvio) >= 0) {
    bool ok = enviarFirebase();
    if (ok) { huboEnvioOk = true; ultimoEnvioOk = millis(); }
    proximoEnvio = millis() + (ok ? FB_INTERVALO_OK : FB_INTERVALO_ERR);
  }

  // Pantalla
  if (now - tDibujo > 300) { tDibujo = now; dibujar(); }
}
