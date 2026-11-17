// Exercise the production UART parser with a simulated DMA ring, without radio hardware.
#include <assert.h>
#include <stdio.h>
#include "bsp/dp32g030/dma.h"

static DMA_Channel_t TestDMA;
#undef DMA_CH0
#define DMA_CH0 (&TestDMA)
#include "app/uart.c"

uint8_t UART_DMA_Buffer[256];

void UART_Send(const void *Buffer, uint32_t Size)
{
	(void)Buffer;
	(void)Size;
	assert(!"Unexpected parser error reply");
}

uint16_t CRC_Calculate(const void *Buffer, uint16_t Size)
{
	const uint8_t *Bytes = Buffer;
	uint16_t Crc = 0;
	for (uint16_t i = 0; i < Size; i++)
	{
		Crc ^= (uint16_t)Bytes[i] << 8;
		for (unsigned Bit = 0; Bit < 8; Bit++)
			Crc = Crc & 0x8000U ? (Crc << 1) ^ 0x1021U : Crc << 1;
	}
	return Crc;
}

static void Reset(uint16_t Position)
{
	memset(UART_DMA_Buffer, 0, sizeof(UART_DMA_Buffer));
	gUART_WriteIndex = Position;
	TestDMA.ST = Position;
	gATActive = false;
	gParserTimeout_10ms = 0;
	gParserLastInputPosition = Position;
	UART_CommandType = UART_COMMAND_NONE;
	bIsEncrypted = false;
}

static void Feed(const void *Buffer, size_t Size)
{
	const uint8_t *Bytes = Buffer;
	for (size_t i = 0; i < Size; i++)
	{
		UART_DMA_Buffer[TestDMA.ST] = Bytes[i];
		TestDMA.ST = DMA_INDEX(TestDMA.ST, 1U);
		assert(TestDMA.ST != gUART_WriteIndex); // Never simulate an undetectable DMA overrun.
	}
}

static void ExpectAT(const void *Buffer, size_t Size)
{
	assert(UART_IsCommandAvailable());
	assert(UART_CommandType == UART_COMMAND_AT);
	assert(gATCommandSize == Size);
	assert(memcmp(UART_Command.Buffer, Buffer, Size) == 0);
	UART_CommandType = UART_COMMAND_NONE;
}

int main(void)
{
	const char *Lines[] = {"AT+BMBLE_doit\r\n", "AT\r\n", "AT+MiX = x_y;RF123;!  \r\n", "AT+中文😀\r\n"};
	for (size_t Line = 0; Line < sizeof(Lines) / sizeof(Lines[0]); Line++)
	{
		const size_t Size = strlen(Lines[Line]);
		for (unsigned Position = 0; Position < 256; Position++)
		{
			for (size_t Split = 1; Split < Size; Split++)
			{
				Reset(Position);
				Feed(Lines[Line], Split);
				assert(!UART_IsCommandAvailable());
				Feed(Lines[Line] + Split, Size - Split);
				ExpectAT(Lines[Line], Size);
				assert(!UART_IsCommandAvailable());
			}
		}
	}

	Reset(250);
	Feed("AT+one\r\nAT+two\r\nRF;", 19);
	ExpectAT("AT+one\r\n", 8);
	ExpectAT("AT+two\r\n", 8);
	assert(UART_IsCommandAvailable() && UART_CommandType == UART_COMMAND_CAT);
	assert(strcmp(CAT_Command, "RF") == 0);

	// Exact 253-byte body, then a 254-byte body containing what looks like CAT.
	char Limit[256];
	memset(Limit, 'x', sizeof(Limit));
	memcpy(Limit, "AT", 2);
	memcpy(Limit + 253, "\r\n", 2);
	Reset(1);
	Feed(Limit, 255);
	ExpectAT(Limit, 255);
	Reset(254);
	Feed(Limit, 240);
	assert(!UART_IsCommandAvailable());
	Feed("RF;xxxxxxxxxxx\r", 15);
	assert(!UART_IsCommandAvailable());
	Feed("\nRF;", 4);
	assert(UART_IsCommandAvailable() && UART_CommandType == UART_COMMAND_CAT);
	assert(strcmp(CAT_Command, "RF") == 0);
	assert(!UART_IsCommandAvailable());

	Reset(10);
	Feed("AT+partial;\r", 12);
	assert(!UART_IsCommandAvailable());
	for (unsigned Tick = 0; Tick < 49; Tick++) UART_TimeSlice10ms();
	assert(gATActive);
	Feed("x", 1);
	assert(!UART_IsCommandAvailable());
	for (unsigned Tick = 0; Tick < 49; Tick++) UART_TimeSlice10ms();
	assert(gATActive);
	UART_TimeSlice10ms();
	assert(!gATActive);
	Feed("RF;AT+ok\r\n", 10);
	assert(UART_IsCommandAvailable() && UART_CommandType == UART_COMMAND_CAT);
	ExpectAT("AT+ok\r\n", 7);

	// Binary payload bytes that resemble AT/CRLF must remain inside the binary frame.
	uint8_t Frame[] = {0xAB, 0xCD, 8, 0, 0x15, 0x06, 4, 0, 'A', 'T', '\r', '\n', 0, 0, 0xDC, 0xBA};
	const uint16_t Crc = CRC_Calculate(Frame + 4, 8);
	Frame[12] = Crc;
	Frame[13] = Crc >> 8;
	for (unsigned Position = 0; Position < 256; Position++)
	{
		Reset(Position);
		Feed("AT+ok\r\n", 7);
		ExpectAT("AT+ok\r\n", 7);
		Feed(Frame, sizeof(Frame));
		assert(UART_IsCommandAvailable() && UART_CommandType == UART_COMMAND_BINARY);
		assert(gBinaryCommandSize == 8);
		Feed("rf;AT\r\n", 7);
		assert(UART_IsCommandAvailable() && UART_CommandType == UART_COMMAND_CAT);
		assert(strcmp(CAT_Command, "RF") == 0);
		ExpectAT("AT\r\n", 4);
	}
	puts("UART parser: AT split/wrap/limit/timeout and CAT/binary coexistence passed");
	return 0;
}
