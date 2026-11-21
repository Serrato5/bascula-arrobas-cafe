/*
 * PRUEBA DE SALIDAS · Báscula de arrobas (Sensórica, USCO 2026-2)
 * Prueba la pantalla LCD, los tres LEDs y el buzzer, sin necesitar la celda.
 *   LCD I2C: SDA -> A4, SCL -> A5
 *   LED amarillo -> D9, verde -> D10, rojo -> D11 (cada uno con 220 ohmios)
 *   Buzzer activo -> D8
 * Si la LCD prende pero no muestra letras: girar el tornillito azul de atrás.
 * Si no muestra nada: cambiar 0x27 por 0x3F y volver a subir.
 */
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

const uint8_t PIN_BUZZER = 8;
const uint8_t PIN_AMAR   = 9;
const uint8_t PIN_VERDE  = 10;
const uint8_t PIN_ROJO   = 11;

LiquidCrystal_I2C lcd(0x27, 16, 2);

void prender(uint8_t pin, const char* nombre) {
  lcd.setCursor(0, 1);
  lcd.print(nombre);
  lcd.print("            ");
  digitalWrite(pin, HIGH);
  delay(700);
  digitalWrite(pin, LOW);
}

void setup() {
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_AMAR, OUTPUT);
  pinMode(PIN_VERDE, OUTPUT);
  pinMode(PIN_ROJO, OUTPUT);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Prueba bascula");
}

void loop() {
  prender(PIN_AMAR,  "LED amarillo");
  prender(PIN_VERDE, "LED verde");
  prender(PIN_ROJO,  "LED rojo");
  lcd.setCursor(0, 1);
  lcd.print("Buzzer          ");
  digitalWrite(PIN_BUZZER, HIGH);
  delay(200);
  digitalWrite(PIN_BUZZER, LOW);
  delay(800);
}
