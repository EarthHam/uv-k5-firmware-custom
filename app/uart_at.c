#include "app/uart_at.h"

bool UART_AT_Push(UART_AT_Parser_t *Parser, uint8_t *Buffer, uint8_t Byte)
{
	const bool Complete = Parser->PreviousCR && Byte == '\n';

	if (Parser->Length < UART_AT_FRAME_MAX_LENGTH)
		Buffer[Parser->Length++] = Byte;
	else
		Parser->Overflow = true;

	Parser->PreviousCR = Byte == '\r';
	return Complete;
}
