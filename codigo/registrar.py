"""
Registro y gráfica en tiempo real — Báscula de arrobas (Sensórica, USCO 2026-2)

Lee las líneas CSV que envía el Arduino por USB, las guarda en un archivo
pesadas_AAAAMMDD_HHMMSS.csv y dibuja en vivo el peso de las dos cadenas
(HX711 y AD620) contra el tiempo.

Uso:
    pip install pyserial matplotlib
    python registrar.py COM5          (Windows; el puerto sale en el IDE de Arduino)
    python registrar.py /dev/ttyACM0  (Linux)

Para cerrar: cerrar la ventana de la gráfica. El CSV queda guardado.
"""
import csv
import sys
import time
from collections import deque

import matplotlib.pyplot as plt
import serial
from matplotlib.animation import FuncAnimation

PUERTO = sys.argv[1] if len(sys.argv) > 1 else "COM5"
BAUDIOS = 115200
VENTANA_S = 60  # segundos visibles en la gráfica

puerto = serial.Serial(PUERTO, BAUDIOS, timeout=1)
time.sleep(2)  # el Arduino se reinicia al abrir el puerto
nombre = time.strftime("pesadas_%Y%m%d_%H%M%S.csv")
archivo = open(nombre, "w", newline="", encoding="utf-8")
tabla = csv.writer(archivo)
tabla.writerow(["t_ms", "kg_hx711", "kg_ad620", "v_ad620", "estado"])

t, hx, ad = deque(maxlen=600), deque(maxlen=600), deque(maxlen=600)

fig, ax = plt.subplots(figsize=(9, 4.5))
linea_hx, = ax.plot([], [], label="HX711 (24 bits)", color="#2f6b4f", lw=2)
linea_ad, = ax.plot([], [], label="AD620 + ADC 10 bits", color="#b86f0b", lw=1.5)
ax.axhline(12.5, ls="--", color="gray", lw=1)
ax.text(0.01, 12.6, "1 arroba = 12,5 kg", transform=ax.get_yaxis_transform(), color="gray", fontsize=8)
ax.set_xlabel("Tiempo (s)")
ax.set_ylabel("Masa (kg)")
ax.set_ylim(-0.5, 21)
ax.grid(alpha=0.3)
ax.legend(loc="upper left")
titulo = ax.set_title("Esperando datos…")


def leer_puerto():
    """Lee todas las líneas disponibles; ignora los mensajes que empiezan con #."""
    while puerto.in_waiting:
        linea = puerto.readline().decode("utf-8", errors="ignore").strip()
        if not linea or linea.startswith("#") or linea.startswith("t_ms"):
            if linea.startswith("#"):
                print(linea)
            continue
        partes = linea.split(",")
        if len(partes) != 5:
            continue
        try:
            t_ms, kg_hx, kg_ad = int(partes[0]), float(partes[1]), float(partes[2])
        except ValueError:
            continue
        tabla.writerow(partes)
        t.append(t_ms / 1000)
        hx.append(kg_hx)
        ad.append(kg_ad)
        titulo.set_text(f"HX711: {kg_hx:.3f} kg   ·   AD620: {kg_ad:.2f} kg   ·   {partes[4]}")


def actualizar(_):
    leer_puerto()
    if t:
        linea_hx.set_data(t, hx)
        linea_ad.set_data(t, ad)
        ax.set_xlim(max(0, t[-1] - VENTANA_S), max(VENTANA_S, t[-1]))
    return linea_hx, linea_ad, titulo


animacion = FuncAnimation(fig, actualizar, interval=100, cache_frame_data=False)
plt.tight_layout()
try:
    plt.show()
finally:
    archivo.close()
    puerto.close()
    print(f"Datos guardados en {nombre}")
