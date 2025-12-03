/*
 * Portable Environmental Data Logger
 * Sensors: DHT12 (T/H), DS3231 (Time), BMP180 (Pressure)
 * * Tento program umoznuje uzivatelovi vybrat rezim:
 * 1. Logger (Zaznam)
 * 2. Reader (Citanie)
 * 3. Eraser (Mazanie)
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <stdio.h>
#include <stdlib.h>
#include "timer.h"
#include "twi.h"
#include <uart.h>
#include <util/delay.h> 
#include <avr/pgmspace.h> 

// -- Adresy I2C zariadeni --
#define DHT_ADR 0x5c
#define RTC_ADR 0x68
#define BMP_ADR 0x77

#define LOG_INTERVAL 5 // Tu je mozne zmenit cas zaznamenavania a tym predlzit celkovy cas zaznamu, 5s je nastavenych len pre potreby testovania
#define TWI_SUCCESS 0

// -- Struktura zaznamu v EEPROM --
typedef struct {
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
    int8_t t_val;        // Teplota (DHT12)
    uint8_t h_val;       // Vlhkost (DHT12)
    uint16_t press_hpa;  // Tlak (BMP180)
} log_record_t;

#define EEPROM_SIZE 1024
#define RECORD_SIZE sizeof(log_record_t)
#define MAX_RECORDS (EEPROM_SIZE / RECORD_SIZE)

// Globalne premenne
uint16_t EEMEM eeprom_index = 0;
volatile uint8_t flag_log = 0;
volatile uint8_t dht_values[5];
volatile uint8_t rtc_time[3];
uint16_t index_ram;

// Premenne pre kalibraciu BMP180
int16_t ac1, ac2, ac3, b1, b2, mb, mc, md;
uint16_t ac4, ac5, ac6;
long b5; 

// Prototypy funkcii 
void start_logger(void);
void run_clearer(void);
void run_reader(void);
void logger_core(void);
void logger_timer_init(void);
uint8_t bcd_to_dec(uint8_t bcd); 
void uart_print_record(log_record_t *rec); 
void read_rtc(void);
void read_dht12(void);

// BMP Funkcie
void bmp180_init(void);
uint16_t read_bmp180_pressure(void);
int16_t read_bmp180_word(uint8_t reg);

int main(void)
{
    unsigned int choice = 0;

    // Inicializacia periferii
    twi_init();
    uart_init(UART_BAUD_SELECT(115200, F_CPU));
    sei();
    
    // Nacitanie kalibracnych koeficientov BMP180 (raz pri starte)
    bmp180_init();

    while (1) 
    {
        uart_puts("\r\n=== DATA LOGGER MENU ===\r\n");
        uart_puts(" 1) START LOGGER (T/H + RTC + Tlak)\r\n");
        uart_puts(" 2) START READER\r\n");
        uart_puts(" 3) START ERASER\r\n");
        uart_puts("Volba: ");
        
        // Cakanie na platny znak
        do { choice = uart_getc(); } while (choice & 0xFF00);
        char cmd = (char)(choice & 0xFF);
        uart_putc(cmd); uart_puts("\r\n");

        if (cmd >= '1' && cmd <= '3') {
             switch (cmd) {
                 case '1': start_logger(); break; // Spusti nekonecne logovanie
                 case '2': run_reader(); break;   // Vypise data
                 case '3': run_clearer(); break;  // Vymaze pamat
             }
        }
        _delay_ms(500);
    }
    return 0;
}

// BMP180 - Vypocet tlaku

// Pomocna funkcia na citanie 16-bit registra
int16_t read_bmp180_word(uint8_t reg) {
    uint8_t buff[2];
    twi_readfrom_mem_into(BMP_ADR, reg, buff, 2);
    return (buff[0] << 8) | buff[1];
}

void bmp180_init(void) {
    // Kontrola ci existuje senzor
    if (twi_test_address(BMP_ADR) != 0) {
        uart_puts("[ERROR] BMP180 Init Failed!\r\n");
        return;
    }
    // Nacitanie kalibracnych konstant z EEPROM senzora
    ac1 = read_bmp180_word(0xAA);
    ac2 = read_bmp180_word(0xAC);
    ac3 = read_bmp180_word(0xAE);
    ac4 = (uint16_t)read_bmp180_word(0xB0);
    ac5 = (uint16_t)read_bmp180_word(0xB2);
    ac6 = (uint16_t)read_bmp180_word(0xB4);
    b1  = read_bmp180_word(0xB6);
    b2  = read_bmp180_word(0xB8);
    mb  = read_bmp180_word(0xBA);
    mc  = read_bmp180_word(0xBC);
    md  = read_bmp180_word(0xBE);
    uart_puts("[OK] BMP180 Calibrated\r\n");
}

uint16_t read_bmp180_pressure(void) {
    long x1, x2, x3, b3, b6, p;
    unsigned long b4, b7;
    long ut;
    unsigned long up;
    uint8_t buff[2];

    // 1. Zaciatok merania teploty
    twi_start(); twi_write((BMP_ADR<<1)|TWI_WRITE);
    twi_write(0xF4); twi_write(0x2E); twi_stop(); 
    _delay_ms(5);
    ut = read_bmp180_word(0xF6);

    // Vypocet teplotnej kompenzacie B5
    x1 = (ut - (long)ac6) * (long)ac5 >> 15;
    x2 = ((long)mc << 11) / (x1 + md);
    b5 = x1 + x2;

    // 2. Start merania tlaku (UP)
    twi_start(); twi_write((BMP_ADR<<1)|TWI_WRITE);
    twi_write(0xF4); twi_write(0x34); twi_stop(); 
    _delay_ms(5);

    // Nacitanie suroveho tlaku
    up = (uint16_t)read_bmp180_word(0xF6); 

    // 3. Bosch algoritmus pre vypocet skutocneho tlaku
    b6 = b5 - 4000;
    x1 = (b2 * (b6 * b6 >> 12)) >> 11;
    x2 = (ac2 * b6) >> 11;
    x3 = x1 + x2;
    b3 = (((((long)ac1) * 4 + x3)) + 2) >> 2;
    
    x1 = (ac3 * b6) >> 13;
    x2 = (b1 * ((b6 * b6) >> 12)) >> 16;
    x3 = ((x1 + x2) + 2) >> 2;
    b4 = (ac4 * (unsigned long)(x3 + 32768)) >> 15;
    
    b7 = ((unsigned long)up - b3) * (50000);
    
    if (b7 < 0x80000000) p = (b7 * 2) / b4;
    else p = (b7 / b4) * 2;
    
    x1 = (p >> 8) * (p >> 8);
    x1 = (x1 * 3038) >> 16;
    x2 = (-7357 * p) >> 16;
    p = p + ((x1 + x2 + 3791) >> 4);

    return (uint16_t)(p / 100); // Prevod na hPa
}

void read_rtc(void) {
    twi_readfrom_mem_into(RTC_ADR, 0x00, (uint8_t*)rtc_time, 3);
}

void read_dht12(void) {
    twi_readfrom_mem_into(DHT_ADR, 0x00, (uint8_t*)dht_values, 5);
}

uint8_t bcd_to_dec(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

void uart_print_record(log_record_t *rec) {
    char buf[40];
    sprintf(buf, "%02u:%02u:%02u | T:%d C | H:%u %% | P:%u hPa\r\n", 
        rec->hour, rec->min, rec->sec, rec->t_val, rec->h_val, rec->press_hpa);
    uart_puts(buf);
}

void logger_core(void)
{
    log_record_t rec;
    uint8_t checksum;

    // Citanie senzorov
    read_rtc();
    read_dht12();
    rec.press_hpa = read_bmp180_pressure(); // Vypocet tlaku

    rec.sec = bcd_to_dec(rtc_time[0]);
    rec.min = bcd_to_dec(rtc_time[1]);
    rec.hour = bcd_to_dec(rtc_time[2]);

    // Kontrola DHT12 checksumu
    checksum = dht_values[0] + dht_values[1] + dht_values[2] + dht_values[3];
    if (checksum == dht_values[4]) {
        rec.h_val = dht_values[0]; 
        rec.t_val = (int8_t)dht_values[2]; 
    } else {
        rec.h_val = 0xFF; rec.t_val = -128; 
        uart_puts("[ERROR] DHT Checksum\r\n");
    }

    // Ukladanie do EEPROM ak nie je plna
    if (index_ram < MAX_RECORDS) {
        eeprom_update_block(&rec, (void*)(index_ram * RECORD_SIZE), RECORD_SIZE);
        index_ram++;
        eeprom_update_word(&eeprom_index, index_ram);
        uart_puts("LOG: "); uart_print_record(&rec);
    } else {
        TIMSK1 &= ~(1 << TOIE1); // Zastavenie casovaca
        uart_puts("[WARNING] Full Memory\r\n"); 
    }
}

void logger_timer_init(void) {
    tim1_ovf_1sec();
    tim1_ovf_enable();
}

/* Spusti rezim Logger, skontroluje pripojenie senzorov, obnovi index pamate a spusti casovac
V nekonecnej slucke caka na flag_log od casovaca.
 */
void start_logger(void) 
{
    uart_puts("\r\n--- LOGGER START ---\r\n");
    // Test pripojenia I2C zariadeni
    if (twi_test_address(RTC_ADR) != 0) uart_puts("[ERROR] RTC missing\r\n");
    if (twi_test_address(DHT_ADR) != 0) uart_puts("[ERROR] DHT12 missing\r\n");

    index_ram = eeprom_read_word(&eeprom_index);
    if (index_ram >= MAX_RECORDS) index_ram = 0; 

    logger_timer_init();

    while (1) {
        if (flag_log) {
            flag_log = 0;
            logger_core();
        }
    }
}
/* Spusti rezim Eraser, vymaze celu EEPROM pamat a vynuluje pocitadlo zaznamov.
 */
void run_clearer(void)
{
    uart_puts("\r\n[INFO] Mazem EEPROM...\r\n");
    // Prepis celej pamate 0xFF
    for (uint16_t i = 0; i < EEPROM_SIZE; i++) {
        eeprom_update_byte((uint8_t*)i, 0xFF);
    }
    eeprom_update_word(&eeprom_index, 0);
}

/* Spusti rezim Reader, precita vsetky ulozene zaznamy z EEPROM a vypise ich na UART v citatelnom formate.
 */
void run_reader(void)
{
    char buf[100];
    uint16_t records;
    log_record_t rec;

    uart_puts("\r\n--- VYPIS DAT ---\r\n");
    records = eeprom_read_word(&eeprom_index);
    if (records > MAX_RECORDS) records = MAX_RECORDS;

    sprintf(buf, "Pocet: %u\r\n", records); uart_puts(buf);
    uart_puts("ID | CAS      | T[C] | H[%] | P[hPa]\r\n");

    for (uint16_t i = 0; i < records; i++)
    {
        eeprom_read_block(&rec, (const void*)(i * RECORD_SIZE), RECORD_SIZE);
        sprintf(buf, "%03u | %02u:%02u:%02u | %4d | %4u | %u\r\n",
                 i, rec.hour, rec.min, rec.sec, rec.t_val, rec.h_val, rec.press_hpa);
        uart_puts(buf);
    }
    uart_puts("--- KONIEC ---\r\n");
}

/* Prerusenie (ISR) pre pretecenie Timer1
 Vola sa kazdu sekundu, pocita cas a po uplynuti intervalu (LOG_INTERVAL) a 
 nastavi priznak 'flag_log', ktory spusti zapis dat v hlavnej slucke.
 */
ISR(TIMER1_OVF_vect) {
    static uint8_t n = 0;
    n++;
    if (n >= LOG_INTERVAL) {
        n = 0;
        flag_log = 1;
    }
}