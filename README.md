#AVR Portable Environmental Logger


## 📋 Popis

Tento projekt slúži ako **prenosný environmentálny logger**, ktorý meria a uchováva:

- Teplotu (°C)  
- Vlhkosť (%)  
- Osvetlenie (lux)  
- Čas (hod, min, sek)  

do **internej EEPROM** mikrokontroléra ATmega328P (Arduino Uno).  
Dáta sa dajú následne čítať cez UART, alebo EEPROM vymazať pre nový záznam.

Projekt nevyžaduje SD kartu ani PC pripojenie pre zápis dát, všetko sa spravuje vnútri mikrokontroléra.

---

## 📦 Použité senzory

| Senzor | I²C adresa | Funkcia |
|--------|------------|---------|
| DHT12  | 0x5C       | Teplota a vlhkosť |
| BH1750 | 0x23       | Osvetlenie (lux) |
| DS3231 | 0x68       | Reálny čas (RTC) |

---

## ⚡ Hardvér

- **Arduino Uno / ATmega328P, 16 MHz**  
- Napájanie 5 V  
- SDA / SCL pre I²C senzory  

**Zapojenie:**
DHT12: SDA -> A4, SCL -> A5, VCC -> 5V, GND -> GND
BH1750: SDA -> A4, SCL -> A5, VCC -> 3.3-5V, GND -> GND
DS3231: SDA -> A4, SCL -> A5, VCC -> 5V, GND -> GND


*(Všetky I²C zariadenia zdieľajú rovnakú zbernicu SDA/SCL)*

---

## 🖥️ Softvér / Súbory

| Súbor | Účel |
|-------|------|
| `main_logger.c` | Logovanie dát do EEPROM (čas, teplota, vlhkosť, osvetlenie) |
| `main_readout.c` | Čítanie a posielanie dát cez UART |
| `main_clear.c` | Vymazanie EEPROM a reset indexu záznamov |
| `twi.h` / `twi.c` | I²C/TWI komunikácia |
| `uart.h` / `uart.c` | UART komunikácia (Peter Fleury) |
| `timer.h` / `timer.c` | Časovač pre meranie intervalu logovania |

---

## 📝 Použitie

### 1️⃣ Logger

1. Nahraj `main_logger.c` do Arduino.  
2. Otvor **Serial Monitor (115200 Bd)** pre debug výpisy.  
3. Logger ukladá dáta do EEPROM každých 5 sekúnd (čas, teplota, vlhkosť, osvetlenie).  

---

### 2️⃣ Read-out (čítanie dát)

1. Nahraj `main_readout.c`.  
2. Otvor Serial Monitor 115200 Bd.  
3. Zobrazia sa všetky záznamy vo formáte:

Time,Temp[C],Hum[%],Light[lx]
12:15:05,23.4,45.2,120
12:15:10,23.4,45.3,118


---

### 3️⃣ Clear EEPROM

1. Nahraj `main_clear.c`.  
2. Serial Monitor 115200 Bd zobrazí:

[INFO] EEPROM Clear Tool
[INFO] Erasing memory...
[OK] EEPROM successfully cleared.
[INFO] You can now start new logging session.


3. EEPROM je teraz pripravená na nové logovanie.  

---

## ⚙️ Poznámky

- Maximálny počet záznamov je ~1024 / sizeof(log_record_t) ≈ 85 (ATmega328P má 1 kB EEPROM).  
- Formát UART výstupu je kompatibilný s Excel alebo CSV viewerom.  
- Projekt používa len knižnice **avr/io.h, avr/interrupt.h, avr/eeprom.h, uart.h, twi.h, stdio.h**.  
- Timer1 je nastavený na 1 Hz, aby sa logovalo každých 5 sekúnd.

