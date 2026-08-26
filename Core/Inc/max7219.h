#ifndef MAX7219_H
#define MAX7219_H

#include "main.h"

// MAX7219 Register
#define MAX7219_REG_DECODE_MODE   0x09
#define MAX7219_REG_INTENSITY     0x0A
#define MAX7219_REG_SCAN_LIMIT    0x0B
#define MAX7219_REG_SHUTDOWN      0x0C
#define MAX7219_REG_DISPLAY_TEST  0x0F

// Größe des Laufschrift-Puffers (Maximal ~30 Zeichen, bei Bedarf erhöhen)
#define SCROLL_BUFFER_SIZE        256

// Funktionen, die von main.c aufgerufen werden können
void MAX7219_Init(void);
void MAX7219_Write(uint8_t reg, uint8_t data);
void MAX7219_LoadString(char* text);
void MAX7219_ShowWindow(uint8_t offset, uint8_t rotation);

// Variable für die aktuelle Länge des Textes (wichtig für die while-Schleife)
extern uint16_t scroll_length;

#endif /* MAX7219_H */
