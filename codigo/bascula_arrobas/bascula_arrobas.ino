/*
 * =====================================================================
 *  BÁSCULA DIGITAL DE ARROBAS PARA CAFÉ (0–20 kg)
 *  Proyecto integrador · Sensórica para Ingenierías · USCO · 2026-2
 *  Integrantes: Ximena del Rocío Moreno · Sebastián Serrato Ramírez ·
 *               Jhoan Sebastián Quiza Durán
 * ---------------------------------------------------------------------
 *  Qué hace:
 *   - Lee la MISMA celda de carga por dos cadenas:
 *       A) HX711 (amplificador + ADC de 24 bits)      -> pines D3/D2
 *       B) AD620 + filtro RC (diseño propio) -> ADC de 10 bits en A0
 *   - Convierte ambas lecturas a kilogramos.
 *   - Decide: LED amarillo (llenando / se pasó), verde + pitido (arroba
 *     completa), rojo + buzzer intermitente (sobrecarga > 20 kg).
 *   - Muestra el peso en una LCD 16x2 por I2C (A4/A5).
 *   - Envía cada lectura por USB (UART, 115200) en formato CSV:
 *       t_ms,kg_hx711,kg_ad620,v_ad620,estado
 *
 *  Comandos por el monitor serie:
 *     t      -> tara (pone en cero las dos cadenas)
 *     c5.0   -> calibra con una masa conocida de 5.0 kg sobre la báscula
 *
 *  Librerías: "HX711" (Bogdan Necula) y "LiquidCrystal I2C" (Frank de Brabander)
 * =====================================================================
 */
#include <HX711.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ----------------------------- Pines ---------------------------------
const uint8_t PIN_HX_DT     = 3;   // HX711 DOUT
const uint8_t PIN_HX_SCK    = 2;   // HX711 PD_SCK
const uint8_t PIN_AD620     = A0;  // salida del filtro RC (cadena B)
const uint8_t PIN_BUZZER    = 8;   // buzzer activo
const uint8_t PIN_LED_AMAR  = 9;
const uint8_t PIN_LED_VERDE = 10;
const uint8_t PIN_LED_ROJO  = 11;
const uint8_t LCD_DIRECCION = 0x27; // si la LCD no muestra nada, probar 0x3F

// ------------------------- Constantes de diseño ----------------------
const float VREF_ADC       = 5.0;    // V, referencia del ADC (AVCC)
const float ADC_MAX        = 1023.0; // 2^10 - 1
const float SENSIB_TEORICA = 0.0826; // V/kg = G·S·Vexc/Cap = 330·1mV/V·5V/20kg
const float CAPACIDAD_KG   = 20.0;   // capacidad de la celda
const float OBJETIVO_KG    = 12.5;   // 1 arroba
const float TOLERANCIA_KG  = 0.10;   // ventana de "arroba completa"
const float HISTERESIS_KG  = 0.05;   // evita parpadeo en los límites

// Muestreo de la cadena B: 60 muestras a 600 Hz = 100 ms = 6 ciclos de 60 Hz
const uint8_t  N_MUESTRAS      = 60;
const uint16_t PERIODO_US      = 1667;   // 1/600 Hz
const uint16_t PERIODO_LCD_MS  = 250;

// ------------------------- Calibración (se ajusta) -------------------
// Valores iniciales; el comando "c<masa>" los recalcula y los imprime
// para copiarlos aquí y que queden fijos.
float factorHX  = 21000.0;          // cuentas del HX711 por kg (provisional)
float sensibAD  = SENSIB_TEORICA;   // V por kg de la cadena B
float vCeroAD   = 1.60;             // V a 0 kg (se mide en la tara)

// ------------------------------ Objetos ------------------------------
HX711 bascula;
LiquidCrystal_I2C lcd(LCD_DIRECCION, 16, 2);

enum Estado { LLENANDO, LISTO, PASADO, SOBRECARGA };
Estado estado = LLENANDO;
const char* NOMBRE_ESTADO[] = { "LLENANDO", "LISTO", "PASADO", "SOBRECARGA" };

float kgHX = 0, kgAD = 0, vAD = 0;
unsigned long tLCD = 0, tBuzzer = 0, finPitido = 0;
bool buzzerOn = false;

// ----------------------------- Funciones -----------------------------

// Cadena B: promedia N_MUESTRAS lecturas de A0 y devuelve voltios
float leerVoltajeAD620() {
  unsigned long suma = 0;
  unsigned long t = micros();
  for (uint8_t i = 0; i < N_MUESTRAS; i++) {
    suma += analogRead(PIN_AD620);
    while (micros() - t < PERIODO_US) { }   // espera al siguiente instante
    t += PERIODO_US;
  }
  return (suma / (float)N_MUESTRAS) * VREF_ADC / ADC_MAX;  // cuentas -> V
}

void tarar() {
  bascula.tare(20);                 // cero del HX711 (promedio de 20)
  float v = 0;
  for (uint8_t i = 0; i < 5; i++) v += leerVoltajeAD620();
  vCeroAD = v / 5.0;                // cero de la cadena B
  Serial.print(F("# Tara lista. V_cero AD620 = "));
  Serial.println(vCeroAD, 4);
}

void calibrar(float masaKg) {
  if (masaKg <= 0) { Serial.println(F("# Masa invalida")); return; }
  float lecturaHX = bascula.get_value(20);        // cuentas sin tara
  factorHX = lecturaHX / masaKg;
  bascula.set_scale(factorHX);
  float v = 0;
  for (uint8_t i = 0; i < 5; i++) v += leerVoltajeAD620();
  sensibAD = (v / 5.0 - vCeroAD) / masaKg;
  Serial.print(F("# Calibrado. factorHX = ")); Serial.print(factorHX, 1);
  Serial.print(F("  sensibAD (V/kg) = "));     Serial.println(sensibAD, 5);
  Serial.println(F("# Copie estos valores en las constantes del codigo."));
}

void atenderComandos() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 't') tarar();
  else if (c == 'c') calibrar(Serial.parseFloat());
}

// Decisión con histéresis, basada en la cadena principal (HX711)
void decidir(float m) {
  switch (estado) {
    case SOBRECARGA:
      if (m < CAPACIDAD_KG - 4 * HISTERESIS_KG) estado = LLENANDO;
      break;
    case LISTO:
      if (fabs(m - OBJETIVO_KG) > TOLERANCIA_KG + HISTERESIS_KG)
        estado = (m < OBJETIVO_KG) ? LLENANDO : PASADO;
      break;
    default:   // LLENANDO o PASADO
      if (fabs(m - OBJETIVO_KG) <= TOLERANCIA_KG) {
        estado = LISTO;
        finPitido = millis() + 200;          // un pitido corto
      } else {
        estado = (m < OBJETIVO_KG) ? LLENANDO : PASADO;
      }
  }
  if (m > CAPACIDAD_KG) estado = SOBRECARGA;
}

void actualizarSalidas() {
  digitalWrite(PIN_LED_AMAR,  estado == LLENANDO || estado == PASADO);
  digitalWrite(PIN_LED_VERDE, estado == LISTO);
  digitalWrite(PIN_LED_ROJO,  estado == SOBRECARGA);

  unsigned long ahora = millis();
  if (estado == SOBRECARGA) {                    // intermitente cada 250 ms
    if (ahora - tBuzzer >= 250) { tBuzzer = ahora; buzzerOn = !buzzerOn; }
  } else {
    buzzerOn = (long)(finPitido - ahora) > 0;    // pitido de "arroba lista"
  }
  digitalWrite(PIN_BUZZER, buzzerOn);
}

void actualizarLCD() {
  if (millis() - tLCD < PERIODO_LCD_MS) return;
  tLCD = millis();
  lcd.setCursor(0, 0);
  lcd.print(F("Peso: "));
  lcd.print(kgHX, 2);
  lcd.print(F(" kg   "));
  lcd.setCursor(0, 1);
  switch (estado) {
    case LLENANDO:   lcd.print(F("Falta ")); lcd.print(OBJETIVO_KG - kgHX, 2); lcd.print(F(" kg    ")); break;
    case LISTO:      lcd.print(F("ARROBA LISTA    ")); break;
    case PASADO:     lcd.print(F("Sobra ")); lcd.print(kgHX - OBJETIVO_KG, 2); lcd.print(F(" kg    ")); break;
    case SOBRECARGA: lcd.print(F("SOBRECARGA!     ")); break;
  }
}

void enviarCSV() {
  Serial.print(millis());   Serial.print(',');
  Serial.print(kgHX, 3);    Serial.print(',');
  Serial.print(kgAD, 3);    Serial.print(',');
  Serial.print(vAD, 4);     Serial.print(',');
  Serial.println(NOMBRE_ESTADO[estado]);
}

// ------------------------------ Programa -----------------------------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED_AMAR, OUTPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_ROJO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  analogReference(DEFAULT);                 // referencia = AVCC (5 V), ratiométrica

  lcd.init();
  lcd.backlight();
  lcd.print(F("Bascula arrobas"));
  lcd.setCursor(0, 1);
  lcd.print(F("Tarando..."));

  bascula.begin(PIN_HX_DT, PIN_HX_SCK);     // canal A, ganancia 128
  bascula.set_scale(factorHX);
  delay(500);                               // asentamiento del HX711
  tarar();
  lcd.clear();
  Serial.println(F("t_ms,kg_hx711,kg_ad620,v_ad620,estado"));
}

void loop() {
  atenderComandos();

  vAD  = leerVoltajeAD620();                 // ~100 ms: marca el ritmo (10 lecturas/s)
  kgAD = (vAD - vCeroAD) / sensibAD;         // V -> kg (cadena B)
  if (bascula.is_ready()) {
    kgHX = bascula.get_units(1);             // kg (cadena A, 10 SPS)
  }

  decidir(kgHX);
  actualizarSalidas();
  actualizarLCD();
  enviarCSV();
}
