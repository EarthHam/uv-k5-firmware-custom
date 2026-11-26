#ifndef APP_UART_AT_H
#define APP_UART_AT_H

#include <stdbool.h>
#include <stdint.h>

#define UART_AT_COMMAND_MAX_LENGTH 253U
#define UART_AT_FRAME_MAX_LENGTH (UART_AT_COMMAND_MAX_LENGTH + 2U)

typedef struct {
	uint16_t Length;
	bool Overflow;
	bool PreviousCR;
} UART_AT_Parser_t;

// Buffer must hold UART_AT_FRAME_MAX_LENGTH bytes. Zero Parser before each line.
bool UART_AT_Push(UART_AT_Parser_t *Parser, uint8_t *Buffer, uint8_t Byte);

#endif
