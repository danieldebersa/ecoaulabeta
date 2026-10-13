# EcoAula Inteligente

Monitor de confort ambiental para aulas universitarias, basado en **ESP32-C3** y **Firebase Realtime Database**.

Mide temperatura y humedad del aula, las muestra en una pantalla OLED y las envía a la nube para consultarlas en vivo desde cualquier lugar.

## Problema que resuelve

Las aulas suelen tener aire viciado, aire acondicionado mal regulado y nadie que se encargue de ventilar. Esto provoca cansancio, falta de atención y gasto de energía innecesario. EcoAula busca medir esas condiciones y volverlas visibles para cambiar hábitos y ahorrar energía.

## Estado actual (Etapa 1)

- Lectura de temperatura y humedad con DHT11
- Pantalla OLED con menú navegable con un solo botón
- Portal de configuración propio (punto de acceso WiFi): se configura la red, la URL de Firebase y el ID del aula sin tocar el código
- Envío periódico a Firebase (dato en vivo + historial)
- LED de estado (configuración, sin WiFi, enviando datos)

## Hardware

| Componente | Pin (ESP32-C3) |
|---|---|
| ESP32-C3 Super Mini con OLED integrada 0.42" (72x40) | SDA GPIO5, SCL GPIO6 |
| Botón | GPIO0 (a GND) |
| LED de estado | GPIO1 (con resistencia 220-330 Ω) |
| DHT11 | GPIO3 |

## Librerías

- U8g2
- DHT sensor library (Adafruit)
- Adafruit Unified Sensor

## Compilación con PlatformIO (VS Code)

`platformio.ini`:

```ini
[env:esp32c3]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
monitor_speed = 115200
lib_deps =
    olikraus/U8g2
    adafruit/DHT sensor library
    adafruit/Adafruit Unified Sensor
build_flags =
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
```

También compila con Arduino IDE (placa *ESP32C3 Dev Module*, *USB CDC On Boot: Enabled*).

## Uso

1. Al primer encendido el dispositivo crea la red **EcoAula-Setup**. Conéctate con el celular y completa el formulario (WiFi, URL de Firebase, ID del aula).
2. Tras guardar, el dispositivo se reinicia y se conecta solo.
3. Para volver a configurar: menú → *Reconfigurar*, o mantener el botón presionado al encender.

### Botón

- Toque corto: siguiente opción / volver
- Mantener 0.8 s: entrar / confirmar

### Menú

1. WiFi: red conectada, IP y señal
2. Sensor DHT: temperatura y humedad
3. Firebase: estado y último envío
4. Reconfigurar

### LED

| Estado | Significado |
|---|---|
| Parpadeo rápido | Modo configuración |
| Apagado | Sin WiFi |
| Parpadeo lento | WiFi OK, Firebase sin confirmar |
| Fijo | Datos llegando a Firebase |

## Firebase

Crea un proyecto en Firebase, activa **Realtime Database** y copia su URL. Estructura de datos:

```
aulas/
  <id_aula>/
    actual/       -> última lectura (se sobrescribe)
    historial/    -> una entrada por envío
```

Cada registro contiene: `temperatura`, `humedad`, `rssi`, `uptime_s`, `ts`.

> **Seguridad:** las reglas de prueba dejan la base abierta. Antes de un despliegue real hay que restringir el acceso.

## Roadmap

- [ ] Sensor de CO₂ real (Sensirion SCD40/SCD41) en lugar de depender solo del DHT11
- [ ] Semáforo visible con LEDs (verde / amarillo / rojo) según nivel de CO₂
- [ ] Panel web en vivo con el mapa de aulas
- [ ] Autenticación segura con Firebase
- [ ] Alertas por buzzer o voz (opcional)
- [ ] Carcasa y calibración con mediciones reales
- [ ] Soporte WPA2-Enterprise (eduroam)

## Licencia

Por definir (por ejemplo MIT).
