/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include <string.h>

#if !defined(ENABLE_OVERLAY)
	#include "ARMCM0.h"
#endif
#ifdef ENABLE_FMRADIO
	#include "app/fm.h"
#endif
#include "app/app.h"
#include "app/common.h"
#include "app/generic.h"
#ifdef ENABLE_REMOTE_CONTROL
	#include "app/remote.h"
#endif
#include "app/uart.h"
#include "app/uart_at.h"
#include "board.h"
#include "bsp/dp32g030/dma.h"
#include "bsp/dp32g030/gpio.h"
#include "dcs.h"
#include "driver/aes.h"
#include "driver/backlight.h"
#include "driver/bk4819.h"
#include "driver/crc.h"
#include "driver/eeprom.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "frequencies.h"
#include "functions.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "version.h"

#if defined(ENABLE_OVERLAY)
	#include "sram-overlay.h"
#endif


#define DMA_INDEX(x, y) (((x) + (y)) % sizeof(UART_DMA_Buffer))
#define CAT_COMMAND_MAX_LENGTH 64
#define CAT_ROLE_ALL_MAX_LENGTH 64U
#define SERIAL_INCOMPLETE_TIMEOUT_10MS 50U

typedef enum {
	UART_COMMAND_NONE = 0,
	UART_COMMAND_BINARY,
	UART_COMMAND_CAT,
	UART_COMMAND_AT
} UART_CommandType_t;

typedef struct {
	uint16_t ID;
	uint16_t Size;
} Header_t;

typedef struct {
	uint8_t  Padding[2];
	uint16_t ID;
} Footer_t;

typedef struct {
	Header_t Header;
	uint32_t Timestamp;
} CMD_0514_t;

typedef struct {
	Header_t Header;
	struct {
		char     Version[16];
		bool     bHasCustomAesKey;
		bool     bIsInLockScreen;
		uint8_t  Padding[2];
		uint32_t Challenge[4];
	} Data;
} REPLY_0514_t;

typedef struct {
	Header_t Header;
	uint16_t Offset;
	uint8_t  Size;
	uint8_t  Padding;
	uint32_t Timestamp;
} CMD_051B_t;

typedef struct {
	Header_t Header;
	struct {
		uint16_t Offset;
		uint8_t  Size;
		uint8_t  Padding;
		uint8_t  Data[128];
	} Data;
} REPLY_051B_t;

typedef struct {
	Header_t Header;
	uint16_t Offset;
	uint8_t  Size;
	bool     bAllowPassword;
	uint32_t Timestamp;
	uint8_t  Data[0];
} CMD_051D_t;

typedef struct {
	Header_t Header;
	struct {
		uint16_t Offset;
	} Data;
} REPLY_051D_t;

typedef struct {
	Header_t Header;
	struct {
		uint16_t RSSI;
		uint8_t  ExNoiseIndicator;
		uint8_t  GlitchIndicator;
	} Data;
} REPLY_0527_t;

typedef struct {
	Header_t Header;
	struct {
		uint16_t Voltage;
		uint16_t Current;
	} Data;
} REPLY_0529_t;

typedef struct {
	Header_t Header;
	uint32_t Response[4];
} CMD_052D_t;

typedef struct {
	Header_t Header;
	struct {
		bool bIsLocked;
		uint8_t Padding[3];
	} Data;
} REPLY_052D_t;

typedef struct {
	Header_t Header;
	uint32_t Timestamp;
} CMD_052F_t;

#ifdef ENABLE_REMOTE_CONTROL
typedef struct {
	Header_t Header;
	uint8_t  Enabled;
} CMD_REMOTE_DISPLAY_SUBSCRIBE_t;

typedef struct __attribute__((packed)) {
	Header_t Header;
	struct {
		uint8_t  Status;
		uint8_t  Enabled;
		uint8_t  DirtyMask;
		uint16_t PageRevision[REMOTE_DISPLAY_PAGE_COUNT];
	} __attribute__((packed)) Data;
} REPLY_REMOTE_DISPLAY_SUBSCRIBE_t;

typedef struct {
	Header_t Header;
	uint8_t  Page;
} CMD_REMOTE_DISPLAY_PAGE_t;

typedef struct {
	Header_t Header;
	struct {
		uint8_t  Status;
		uint8_t  Page;
		uint16_t Revision;
		uint16_t Crc;
		uint8_t  Pixels[REMOTE_DISPLAY_PAGE_SIZE];
	} Data;
} REPLY_REMOTE_DISPLAY_PAGE_t;

typedef struct __attribute__((packed)) {
	Header_t Header;
	struct {
		uint8_t  DirtyMask;
		uint16_t PageRevision[REMOTE_DISPLAY_PAGE_COUNT];
	} __attribute__((packed)) Data;
} EVENT_REMOTE_DISPLAY_CHANGED_t;

typedef struct {
	Header_t Header;
	uint8_t  Key;
	uint8_t  Action;
} CMD_REMOTE_KEY_EVENT_t;

typedef struct {
	Header_t Header;
	struct {
		uint8_t Status;
		uint8_t Key;
		uint8_t Action;
		uint8_t Padding;
	} Data;
} REPLY_REMOTE_KEY_EVENT_t;

typedef struct {
	Header_t Header;
	struct {
		uint8_t Status;
		uint8_t Padding[3];
	} Data;
} REPLY_REMOTE_RELEASE_ALL_t;

_Static_assert(sizeof(REPLY_REMOTE_DISPLAY_SUBSCRIBE_t) == 23, "Remote subscribe layout");
_Static_assert(sizeof(REPLY_REMOTE_DISPLAY_PAGE_t) == 138, "Remote page layout");
_Static_assert(sizeof(EVENT_REMOTE_DISPLAY_CHANGED_t) == 21, "Remote event layout");
#endif

static const uint8_t Obfuscation[16] =
{
	0x16, 0x6C, 0x14, 0xE6, 0x2E, 0x91, 0x0D, 0x40, 0x21, 0x35, 0xD5, 0x40, 0x13, 0x03, 0xE9, 0x80
};

static union
{
	uint8_t Buffer[256];
	struct
	{
		Header_t Header;
		uint8_t Data[252];
	};
} UART_Command;

static uint32_t Timestamp;
static uint16_t gUART_WriteIndex;
static uint16_t gBinaryCommandSize;
static bool     bIsEncrypted = true;
static UART_CommandType_t UART_CommandType;
static char CAT_Command[CAT_COMMAND_MAX_LENGTH + 1];
static uint32_t CAT_LastFrequency[2];
static char CAT_LastRoleState[CAT_ROLE_ALL_MAX_LENGTH + 1U];
static uint8_t CAT_LastRoleStateLength;
static bool CAT_LastRoleStateValid;
static bool CAT_RoleStateInitialized;
static uint8_t CAT_LastSquelchLevel;
static bool CAT_SquelchStateInitialized;
static uint16_t gParserLastInputPosition;
static uint8_t gParserTimeout_10ms;
static UART_AT_Parser_t gATParser;
static bool gATActive;
static uint16_t gATCommandSize;

static void CAT_RecordRoleState(void);
static void CAT_NotifyRoleStateChanges(void);
static void CAT_RecordSquelchState(void);
static void CAT_NotifySquelchStateChanges(void);

static void UART_ConsumeBytes(uint16_t Count)
{
	while (Count-- > 0)
	{
		UART_DMA_Buffer[gUART_WriteIndex] = 0;
		gUART_WriteIndex = DMA_INDEX(gUART_WriteIndex, 1);
	}
}

static bool CAT_ParseUnsigned(const char *pString, uint8_t Length, uint32_t *pValue)
{
	uint32_t Value = 0;

	for (uint8_t i = 0; i < Length; i++)
	{
		if (pString[i] < '0' || pString[i] > '9')
			return false;

		Value = (Value * 10U) + (uint32_t)(pString[i] - '0');
	}

	*pValue = Value;
	return true;
}

static void CAT_WriteDecimal(char *pString, uint32_t Value, uint8_t Digits)
{
	while (Digits-- > 0)
	{
		pString[Digits] = '0' + (Value % 10U);
		Value /= 10U;
	}
}

static void CAT_SendError(void)
{
	static const char Error[] = "?;";
	UART_Send(Error, sizeof(Error) - 1);
}

static void CAT_SendNamedError(const char *pName, const uint8_t NameLength, const uint8_t Error)
{
	char Reply[8] = {'E', 0, 0, 0, '0', '0', ';', 0};
	memcpy(&Reply[1], pName, NameLength);
	Reply[1U + NameLength] = '0' + Error / 10U;
	Reply[2U + NameLength] = '0' + Error % 10U;
	Reply[3U + NameLength] = ';';
	UART_Send(Reply, 4U + NameLength);
}

enum {
	CAT_ROLE_RX = 0,
	CAT_ROLE_TX
};

enum {
	CAT_ROLE_ERROR_SYNTAX = 1,
	CAT_ROLE_ERROR_UNSUPPORTED,
	CAT_ROLE_ERROR_FREQUENCY,
	CAT_ROLE_ERROR_BUSY,
	CAT_ROLE_ERROR_STATE
};

static void CAT_SendRoleError(const char Role, const char Field, const uint8_t Error)
{
	char Reply[6] = {'E', Role, Field, '0', '0', ';'};
	Reply[3] = '0' + Error / 10U;
	Reply[4] = '0' + Error % 10U;
	UART_Send(Reply, sizeof(Reply));
}

static void CAT_FixVfoPointers(VFO_Info_t *pVfo)
{
	if (pVfo->FrequencyReverse)
	{
		pVfo->pRX = &pVfo->freq_config_TX;
		pVfo->pTX = &pVfo->freq_config_RX;
	}
	else
	{
		pVfo->pRX = &pVfo->freq_config_RX;
		pVfo->pTX = &pVfo->freq_config_TX;
	}
}

static bool CAT_CanReconfigure(void)
{
	return gCurrentFunction != FUNCTION_TRANSMIT;
}

static void CAT_Reconfigure(void)
{
	RADIO_SelectVfos();
	RADIO_SetupRegisters(true);
	gUpdateDisplay = true;
	gUpdateStatus = true;
}

static void CAT_TuneFrequency(const uint32_t Frequency)
{
	BK4819_SetFrequency(Frequency);

	// REG_38/39 hold the requested frequency, but an active receiver needs a
	// new VCO calibration edge before using it. Toggle only the calibration
	// bit so the RX link, RX DSP and AF DAC remain enabled throughout.
	const uint16_t RxControl = BK4819_ReadRegister(BK4819_REG_30);
	BK4819_WriteRegister(BK4819_REG_30, RxControl & ~BK4819_REG_30_ENABLE_VCO_CALIB);
	BK4819_WriteRegister(BK4819_REG_30, RxControl);
}

void UART_InitCATState(void)
{
	CAT_LastFrequency[0] = gEeprom.VfoInfo[0].freq_config_RX.Frequency;
	CAT_LastFrequency[1] = gEeprom.VfoInfo[1].freq_config_RX.Frequency;
	CAT_RecordRoleState();
	CAT_RecordSquelchState();
}

void UART_NotifyFrequencyChanges(void)
{
	if (SerialConfigInProgress())
		return;
	CAT_NotifyRoleStateChanges();
	CAT_NotifySquelchStateChanges();
}

static bool CAT_SetFrequency(uint8_t Vfo, uint32_t FrequencyHz)
{
	VFO_Info_t *pVfo = &gEeprom.VfoInfo[Vfo];
	const uint32_t Frequency = (FrequencyHz + 5U) / 10U;
	const uint32_t PreviousRxFrequency = pVfo->pRX->Frequency;
	const FREQUENCY_Band_t PreviousBand = FREQUENCY_GetBand(PreviousRxFrequency);
	const bool WasFrequencyReversed = pVfo->FrequencyReverse;
	const bool IsActiveRxVfo = pVfo == gRxVfo;

	if (!CAT_CanReconfigure() || RX_freq_check(Frequency) != 0)
		return false;

	const FREQUENCY_Band_t Band = FREQUENCY_GetBand(Frequency);
	const uint8_t Channel = FREQ_CHANNEL_FIRST + Band;

	gEeprom.ScreenChannel[Vfo] = Channel;
	gEeprom.FreqChannel[Vfo] = Channel;
	pVfo->CHANNEL_SAVE = Channel;
	pVfo->Band = Band;
	pVfo->FrequencyReverse = false;
	pVfo->TX_OFFSET_FREQUENCY_DIRECTION = TX_OFFSET_FREQUENCY_DIRECTION_OFF;
	pVfo->freq_config_RX.Frequency = Frequency;
	CAT_FixVfoPointers(pVfo);
	RADIO_ApplyOffset(pVfo);
	RADIO_ConfigureSquelchAndOutputPower(pVfo);

	if (IsActiveRxVfo && (WasFrequencyReversed || Frequency != PreviousRxFrequency))
	{
		const bool SameRxPath = (PreviousRxFrequency < 28000000U) == (Frequency < 28000000U);

		if (!WasFrequencyReversed && PreviousBand == Band && SameRxPath)
		{
			// Doppler updates stay inside the current receive configuration.
			// Retune the synthesizer without muting audio or restarting RX.
			CAT_TuneFrequency(Frequency);
		}
		else
		{
			// Reversing the VFO or crossing an RF configuration boundary still
			// requires the normal, complete receiver setup.
			CAT_Reconfigure();
		}
	}

	gUpdateDisplay = true;
	gUpdateStatus = true;
	// The host initiated this change, so do not report it later as a local edit.
	CAT_LastFrequency[Vfo] = Frequency;
	return true;
}

static bool CAT_IsRoleCrossBand(void)
{
	return gEeprom.CROSS_BAND_RX_TX != CROSS_BAND_OFF;
}

static uint8_t CAT_GetRoleVfo(const uint8_t Role)
{
	if (Role == CAT_ROLE_TX || !CAT_IsRoleCrossBand())
		return gEeprom.TX_VFO;
	return gEeprom.TX_VFO ^ 1U;
}

static void CAT_SendRoleRoute(void)
{
	static const char Main[] = "TRMAIN;";
	static const char Cross[] = "TRCROSS;";
	if (gEeprom.DUAL_WATCH != DUAL_WATCH_OFF)
	{
		CAT_SendRoleError('T', 'R', CAT_ROLE_ERROR_STATE);
		return;
	}
	UART_Send(CAT_IsRoleCrossBand() ? Cross : Main,
		CAT_IsRoleCrossBand() ? sizeof(Cross) - 1U : sizeof(Main) - 1U);
}

static bool CAT_SetRoleRoute(const bool Cross)
{
	if (!CAT_CanReconfigure())
		return false;
	gEeprom.RX_VFO = 0;
	gEeprom.TX_VFO = Cross ? 1 : 0;
	gEeprom.CROSS_BAND_RX_TX = Cross ? CROSS_BAND_CHAN_B : CROSS_BAND_OFF;
	gEeprom.DUAL_WATCH = DUAL_WATCH_OFF;
	CAT_Reconfigure();
	return true;
}

static void CAT_ApplyMainTxFrequency(VFO_Info_t *pVfo, const uint32_t FrequencyHz)
{
	const uint32_t RxFrequency = pVfo->freq_config_RX.Frequency;
	const uint32_t TxFrequency = (FrequencyHz + 5U) / 10U;
	pVfo->TX_OFFSET_FREQUENCY_DIRECTION = TxFrequency == RxFrequency
		? TX_OFFSET_FREQUENCY_DIRECTION_OFF : (TxFrequency > RxFrequency
			? TX_OFFSET_FREQUENCY_DIRECTION_ADD : TX_OFFSET_FREQUENCY_DIRECTION_SUB);
	pVfo->TX_OFFSET_FREQUENCY = TxFrequency > RxFrequency
		? TxFrequency - RxFrequency : RxFrequency - TxFrequency;
	RADIO_ApplyOffset(pVfo);
	RADIO_ConfigureSquelchAndOutputPower(pVfo);
}

static bool CAT_SetRoleFrequency(const uint8_t Role, const uint32_t FrequencyHz)
{
	const uint8_t Vfo = CAT_GetRoleVfo(Role);
	if (CAT_IsRoleCrossBand())
		return CAT_SetFrequency(Vfo, FrequencyHz);

	VFO_Info_t *pVfo = &gEeprom.VfoInfo[Vfo];
	const bool WasFrequencyReversed = pVfo->FrequencyReverse;
	FREQ_Config_t Rx = *pVfo->pRX;
	const FREQ_Config_t Tx = *pVfo->pTX;
	const uint32_t RxFrequencyHz = Role == CAT_ROLE_RX ? FrequencyHz : Rx.Frequency * 10U;
	const uint32_t TxFrequencyHz = Role == CAT_ROLE_TX ? FrequencyHz : Tx.Frequency * 10U;
	if (!CAT_SetFrequency(Vfo, RxFrequencyHz))
		return false;

	// Preserve effective RX/TX tones when normalizing a reversed VFO.
	Rx.Frequency = pVfo->freq_config_RX.Frequency;
	pVfo->freq_config_RX = Rx;
	pVfo->freq_config_TX = Tx;
	CAT_ApplyMainTxFrequency(pVfo, TxFrequencyHz);
	if (WasFrequencyReversed)
		CAT_Reconfigure();
	return true;
}

static void CAT_SendRoleFrequency(const uint8_t Role)
{
	const VFO_Info_t *pVfo = &gEeprom.VfoInfo[CAT_GetRoleVfo(Role)];
	const uint32_t FrequencyHz = (Role == CAT_ROLE_RX ? pVfo->pRX : pVfo->pTX)->Frequency * 10U;
	char Reply[12] = {Role == CAT_ROLE_RX ? 'R' : 'T', 'F', '0', '0', '0', '0', '0', '0', '0', '0', '0', ';'};
	CAT_WriteDecimal(&Reply[2], FrequencyHz, 9);
	UART_Send(Reply, sizeof(Reply));
}

static const char *CAT_GetVfoMode(const VFO_Info_t *pVfo)
{
	const bool Narrow = pVfo->CHANNEL_BANDWIDTH != BK4819_FILTER_BW_WIDE;
	if (pVfo->Modulation == MODULATION_USB) return "USB";
	if (pVfo->Modulation == MODULATION_AM) return Narrow ? "NAM" : "AM";
	return Narrow ? "NFM" : "FM";
}

static const char *CAT_GetRoleMode(const uint8_t Role)
{
	return CAT_GetVfoMode(&gEeprom.VfoInfo[CAT_GetRoleVfo(Role)]);
}

static bool CAT_SetRoleMode(const uint8_t Role, const char *Mode, const uint8_t Length)
{
	VFO_Info_t *pVfo = &gEeprom.VfoInfo[CAT_GetRoleVfo(Role)];
	if (!CAT_CanReconfigure())
		return false;
	if (Length == 2 && memcmp(Mode, "FM", 2) == 0)
	{
		pVfo->Modulation = MODULATION_FM;
		pVfo->CHANNEL_BANDWIDTH = BK4819_FILTER_BW_WIDE;
	}
	else if (Length == 3 && memcmp(Mode, "NFM", 3) == 0)
	{
		pVfo->Modulation = MODULATION_FM;
		pVfo->CHANNEL_BANDWIDTH = BK4819_FILTER_BW_NARROW;
	}
	else if (Length == 2 && memcmp(Mode, "AM", 2) == 0)
	{
		pVfo->Modulation = MODULATION_AM;
		pVfo->CHANNEL_BANDWIDTH = BK4819_FILTER_BW_WIDE;
	}
	else if (Length == 3 && memcmp(Mode, "NAM", 3) == 0)
	{
		pVfo->Modulation = MODULATION_AM;
		pVfo->CHANNEL_BANDWIDTH = BK4819_FILTER_BW_NARROW;
	}
	else if (Length == 3 && memcmp(Mode, "USB", 3) == 0)
	{
		pVfo->Modulation = MODULATION_USB;
		pVfo->CHANNEL_BANDWIDTH = BK4819_FILTER_BW_WIDE;
	}
	else
	{
		return false;
	}
	CAT_Reconfigure();
	return true;
}

static void CAT_SendRoleMode(const uint8_t Role)
{
	const char *Mode = CAT_GetRoleMode(Role);
	const size_t ModeLength = strlen(Mode);
	char Reply[6] = {Role == CAT_ROLE_RX ? 'R' : 'T', 'M', 0, 0, 0, ';'};
	memcpy(&Reply[2], Mode, ModeLength);
	Reply[2 + ModeLength] = ';';
	UART_Send(Reply, 3U + ModeLength);
}

static int8_t CAT_FindCtcssCode(const uint32_t Tone)
{
	if (Tone == 0)
		return -1;
	for (uint8_t Code = 0; Code < ARRAY_SIZE(CTCSS_Options); Code++)
		if (CTCSS_Options[Code] == Tone)
			return (int8_t)Code;
	return -2;
}

static FREQ_Config_t *CAT_GetRoleCtcssConfig(const uint8_t Role)
{
	VFO_Info_t *pVfo = &gEeprom.VfoInfo[CAT_GetRoleVfo(Role)];
	return Role == CAT_ROLE_RX ? pVfo->pRX : pVfo->pTX;
}

static uint32_t CAT_GetCtcssTone(const FREQ_Config_t *pConfig)
{
	return pConfig->CodeType == CODE_TYPE_CONTINUOUS_TONE && pConfig->Code < ARRAY_SIZE(CTCSS_Options)
		? CTCSS_Options[pConfig->Code] : 0U;
}

static uint32_t CAT_GetRoleCtcssTone(const uint8_t Role)
{
	return CAT_GetCtcssTone(CAT_GetRoleCtcssConfig(Role));
}

typedef struct
{
	bool Cross;
	uint32_t RxFrequencyHz;
	uint32_t TxFrequencyHz;
	uint8_t RxModulation;
	uint8_t TxModulation;
	uint8_t RxBandwidth;
	uint8_t TxBandwidth;
	int8_t RxCtcssCode;
	int8_t TxCtcssCode;
} CAT_RoleAll_t;

static void CAT_SendRoleAllError(const uint8_t Error)
{
	char Reply[7] = {'E', 'T', 'R', 'A', '0', '0', ';'};
	Reply[4] = '0' + Error / 10U;
	Reply[5] = '0' + Error % 10U;
	UART_Send(Reply, sizeof(Reply));
}

static uint8_t CAT_BuildRoleAllForSelection(char Reply[CAT_ROLE_ALL_MAX_LENGTH + 1U],
	const bool Cross, const uint8_t TxVfo)
{
	if (TxVfo > 1U)
		return 0;

	const uint8_t RxVfo = Cross ? TxVfo ^ 1U : TxVfo;
	const VFO_Info_t *pRxVfo = &gEeprom.VfoInfo[RxVfo];
	const VFO_Info_t *pTxVfo = &gEeprom.VfoInfo[TxVfo];
	const uint32_t RxTone = CAT_GetCtcssTone(pRxVfo->pRX);
	const uint32_t TxTone = CAT_GetCtcssTone(pTxVfo->pTX);
	if (pRxVfo->pRX->Frequency == 0U || pRxVfo->pRX->Frequency > 99999999U ||
		pTxVfo->pTX->Frequency == 0U || pTxVfo->pTX->Frequency > 99999999U)
		return 0;

	uint8_t Length = 0;
	memcpy(&Reply[Length], "TRA", 3U);
	Length += 3U;
	const char *Route = Cross ? "CROSS" : "MAIN";
	const uint8_t RouteLength = Cross ? 5U : 4U;
	memcpy(&Reply[Length], Route, RouteLength);
	Length += RouteLength;
	Reply[Length++] = ',';

	CAT_WriteDecimal(&Reply[Length], pRxVfo->pRX->Frequency * 10U, 9U);
	Length += 9U;
	Reply[Length++] = ',';
	const char *RxMode = CAT_GetVfoMode(pRxVfo);
	const uint8_t RxModeLength = strlen(RxMode);
	memcpy(&Reply[Length], RxMode, RxModeLength);
	Length += RxModeLength;
	Reply[Length++] = ',';
	CAT_WriteDecimal(&Reply[Length], RxTone, 4U);
	Length += 4U;
	Reply[Length++] = ',';
	CAT_WriteDecimal(&Reply[Length], pTxVfo->pTX->Frequency * 10U, 9U);
	Length += 9U;
	Reply[Length++] = ',';
	const char *TxMode = CAT_GetVfoMode(pTxVfo);
	const uint8_t TxModeLength = strlen(TxMode);
	memcpy(&Reply[Length], TxMode, TxModeLength);
	Length += TxModeLength;
	Reply[Length++] = ',';
	CAT_WriteDecimal(&Reply[Length], TxTone, 4U);
	Length += 4U;
	Reply[Length++] = ';';
	Reply[Length] = 0;
	return Length;
}

static uint8_t CAT_BuildRoleAll(char Reply[CAT_ROLE_ALL_MAX_LENGTH + 1U])
{
	if (gEeprom.DUAL_WATCH != DUAL_WATCH_OFF)
		return 0;
	return CAT_BuildRoleAllForSelection(Reply, CAT_IsRoleCrossBand(), gEeprom.TX_VFO);
}

static void CAT_RecordRoleState(void)
{
	const uint8_t Length = CAT_BuildRoleAll(CAT_LastRoleState);
	CAT_LastRoleStateLength = Length;
	CAT_LastRoleStateValid = Length != 0U;
	CAT_RoleStateInitialized = true;
}

static void CAT_NotifyRoleStateChanges(void)
{
	char State[CAT_ROLE_ALL_MAX_LENGTH + 1U];
	const uint8_t Length = CAT_BuildRoleAll(State);
	const bool Valid = Length != 0U;

	if (!CAT_RoleStateInitialized)
	{
		CAT_RecordRoleState();
		return;
	}
	if (Valid == CAT_LastRoleStateValid &&
		(!Valid || (Length == CAT_LastRoleStateLength && memcmp(State, CAT_LastRoleState, Length) == 0)))
		return;

	CAT_LastRoleStateValid = Valid;
	CAT_LastRoleStateLength = Length;
	if (Valid)
		memcpy(CAT_LastRoleState, State, Length + 1U);
#ifdef ENABLE_REMOTE_CONTROL
	if (REMOTE_IsInitialized())
	{
		if (Valid)
			UART_Send(State, Length);
		else
			CAT_SendRoleAllError(CAT_ROLE_ERROR_STATE);
	}
#endif
}

static void CAT_SendSquelch(void)
{
	const char Reply[] = {'S', 'Q', '0' + gEeprom.SQUELCH_LEVEL, ';'};
	UART_Send(Reply, sizeof(Reply));
}

static void CAT_RecordSquelchState(void)
{
	CAT_LastSquelchLevel = gEeprom.SQUELCH_LEVEL;
	CAT_SquelchStateInitialized = true;
}

static void CAT_NotifySquelchStateChanges(void)
{
	if (!CAT_SquelchStateInitialized)
	{
		CAT_RecordSquelchState();
		return;
	}
	if (CAT_LastSquelchLevel == gEeprom.SQUELCH_LEVEL)
		return;

	CAT_RecordSquelchState();
#ifdef ENABLE_REMOTE_CONTROL
	if (REMOTE_IsInitialized())
		CAT_SendSquelch();
#endif
}

static bool CAT_TakeRoleAllField(const char **pCursor, const char **pField,
	uint8_t *pLength, const bool Last)
{
	const char *Cursor = *pCursor;
	const char *Separator = strchr(Cursor, ',');
	if ((!Last && Separator == NULL) || (Last && Separator != NULL))
		return false;
	const char *End = Last ? Cursor + strlen(Cursor) : Separator;
	const size_t Length = End - Cursor;
	if (Length == 0U || Length > UINT8_MAX)
		return false;
	*pField = Cursor;
	*pLength = Length;
	*pCursor = Last ? End : Separator + 1;
	return true;
}

static bool CAT_ParseRoleAllMode(const char *Mode, const uint8_t Length,
	uint8_t *pModulation, uint8_t *pBandwidth)
{
	if (Length == 2U && memcmp(Mode, "FM", 2U) == 0)
	{
		*pModulation = MODULATION_FM;
		*pBandwidth = BK4819_FILTER_BW_WIDE;
	}
	else if (Length == 3U && memcmp(Mode, "NFM", 3U) == 0)
	{
		*pModulation = MODULATION_FM;
		*pBandwidth = BK4819_FILTER_BW_NARROW;
	}
	else if (Length == 2U && memcmp(Mode, "AM", 2U) == 0)
	{
		*pModulation = MODULATION_AM;
		*pBandwidth = BK4819_FILTER_BW_WIDE;
	}
	else if (Length == 3U && memcmp(Mode, "NAM", 3U) == 0)
	{
		*pModulation = MODULATION_AM;
		*pBandwidth = BK4819_FILTER_BW_NARROW;
	}
	else if (Length == 3U && memcmp(Mode, "USB", 3U) == 0)
	{
		*pModulation = MODULATION_USB;
		*pBandwidth = BK4819_FILTER_BW_WIDE;
	}
	else
	{
		return false;
	}
	return true;
}

static uint8_t CAT_ParseRoleAll(const char *pCommand, CAT_RoleAll_t *pState)
{
	const char *Cursor = &pCommand[3];
	const char *Fields[7];
	uint8_t Lengths[7];
	for (uint8_t i = 0; i < 7U; i++)
		if (!CAT_TakeRoleAllField(&Cursor, &Fields[i], &Lengths[i], i == 6U))
			return CAT_ROLE_ERROR_SYNTAX;

	if (Lengths[0] == 4U && memcmp(Fields[0], "MAIN", 4U) == 0)
		pState->Cross = false;
	else if (Lengths[0] == 5U && memcmp(Fields[0], "CROSS", 5U) == 0)
		pState->Cross = true;
	else
		return CAT_ROLE_ERROR_UNSUPPORTED;

	if (Lengths[1] != 9U || Lengths[3] != 4U || Lengths[4] != 9U || Lengths[6] != 4U ||
		!CAT_ParseUnsigned(Fields[1], 9U, &pState->RxFrequencyHz) ||
		!CAT_ParseUnsigned(Fields[4], 9U, &pState->TxFrequencyHz))
		return CAT_ROLE_ERROR_SYNTAX;

	uint32_t RxTone;
	uint32_t TxTone;
	if (!CAT_ParseUnsigned(Fields[3], 4U, &RxTone) ||
		!CAT_ParseUnsigned(Fields[6], 4U, &TxTone))
		return CAT_ROLE_ERROR_SYNTAX;
	pState->RxCtcssCode = CAT_FindCtcssCode(RxTone);
	pState->TxCtcssCode = CAT_FindCtcssCode(TxTone);
	if (pState->RxCtcssCode == -2 || pState->TxCtcssCode == -2 ||
		!CAT_ParseRoleAllMode(Fields[2], Lengths[2], &pState->RxModulation, &pState->RxBandwidth) ||
		!CAT_ParseRoleAllMode(Fields[5], Lengths[5], &pState->TxModulation, &pState->TxBandwidth))
		return CAT_ROLE_ERROR_UNSUPPORTED;

	if (!pState->Cross && (pState->RxModulation != pState->TxModulation ||
		pState->RxBandwidth != pState->TxBandwidth))
		return CAT_ROLE_ERROR_UNSUPPORTED;
	if (RX_freq_check((pState->RxFrequencyHz + 5U) / 10U) != 0 ||
		RX_freq_check((pState->TxFrequencyHz + 5U) / 10U) != 0)
		return CAT_ROLE_ERROR_FREQUENCY;
	return 0U;
}

static void CAT_ApplyRoleAllFrequency(const uint8_t Vfo, const uint32_t FrequencyHz)
{
	VFO_Info_t *pVfo = &gEeprom.VfoInfo[Vfo];
	const uint32_t Frequency = (FrequencyHz + 5U) / 10U;
	const FREQUENCY_Band_t Band = FREQUENCY_GetBand(Frequency);
	const uint8_t Channel = FREQ_CHANNEL_FIRST + Band;
	gEeprom.ScreenChannel[Vfo] = Channel;
	gEeprom.FreqChannel[Vfo] = Channel;
	pVfo->CHANNEL_SAVE = Channel;
	pVfo->Band = Band;
	pVfo->FrequencyReverse = false;
	pVfo->TX_OFFSET_FREQUENCY_DIRECTION = TX_OFFSET_FREQUENCY_DIRECTION_OFF;
	pVfo->freq_config_RX.Frequency = Frequency;
	CAT_FixVfoPointers(pVfo);
	RADIO_ApplyOffset(pVfo);
	RADIO_ConfigureSquelchAndOutputPower(pVfo);
	CAT_LastFrequency[Vfo] = Frequency;
}

static void CAT_ApplyRoleAllTone(FREQ_Config_t *pConfig, const int8_t Code)
{
	pConfig->CodeType = Code < 0 ? CODE_TYPE_OFF : CODE_TYPE_CONTINUOUS_TONE;
	pConfig->Code = Code < 0 ? 0U : (uint8_t)Code;
}

static void CAT_ApplyRoleAll(const CAT_RoleAll_t *pState)
{
	gEeprom.RX_VFO = 0U;
	gEeprom.TX_VFO = pState->Cross ? 1U : 0U;
	gEeprom.CROSS_BAND_RX_TX = pState->Cross ? CROSS_BAND_CHAN_B : CROSS_BAND_OFF;
	gEeprom.DUAL_WATCH = DUAL_WATCH_OFF;

	CAT_ApplyRoleAllFrequency(0U, pState->RxFrequencyHz);
	VFO_Info_t *pRxVfo = &gEeprom.VfoInfo[0];
	pRxVfo->Modulation = pState->RxModulation;
	pRxVfo->CHANNEL_BANDWIDTH = pState->RxBandwidth;
	CAT_ApplyRoleAllTone(&pRxVfo->freq_config_RX, pState->RxCtcssCode);

	if (pState->Cross)
	{
		CAT_ApplyRoleAllFrequency(1U, pState->TxFrequencyHz);
		VFO_Info_t *pTxVfo = &gEeprom.VfoInfo[1];
		pTxVfo->Modulation = pState->TxModulation;
		pTxVfo->CHANNEL_BANDWIDTH = pState->TxBandwidth;
		CAT_ApplyRoleAllTone(&pTxVfo->freq_config_TX, pState->TxCtcssCode);
	}
	else
	{
		CAT_ApplyMainTxFrequency(pRxVfo, pState->TxFrequencyHz);
		CAT_ApplyRoleAllTone(&pRxVfo->freq_config_TX, pState->TxCtcssCode);
	}
	CAT_Reconfigure();
}

static void CAT_HandleRoleAllCommand(const char *pCommand, const uint8_t Length)
{
	if (Length == 3U)
	{
		char Reply[CAT_ROLE_ALL_MAX_LENGTH + 1U];
		const uint8_t ReplyLength = CAT_BuildRoleAll(Reply);
		if (ReplyLength == 0U)
			CAT_SendRoleAllError(CAT_ROLE_ERROR_STATE);
		else
			UART_Send(Reply, ReplyLength);
		CAT_RecordRoleState();
		return;
	}
	if (!CAT_CanReconfigure())
	{
		CAT_SendRoleAllError(CAT_ROLE_ERROR_BUSY);
		return;
	}

	CAT_RoleAll_t State;
	const uint8_t Error = CAT_ParseRoleAll(pCommand, &State);
	if (Error != 0U)
	{
		CAT_SendRoleAllError(Error);
		return;
	}
	CAT_ApplyRoleAll(&State);
	CAT_RecordRoleState();
	UART_Send(CAT_LastRoleState, CAT_LastRoleStateLength);
}

static void CAT_SendRoleCtcss(const uint8_t Role)
{
	char Reply[7] = {Role == CAT_ROLE_RX ? 'R' : 'T', 'C', '0', '0', '0', '0', ';'};
	CAT_WriteDecimal(&Reply[2], CAT_GetRoleCtcssTone(Role), 4);
	UART_Send(Reply, sizeof(Reply));
}

static bool CAT_SetRoleCtcss(const uint8_t Role, const uint32_t Tone)
{
	const int8_t Code = CAT_FindCtcssCode(Tone);
	if (!CAT_CanReconfigure() || Code == -2)
		return false;

	FREQ_Config_t *pConfig = CAT_GetRoleCtcssConfig(Role);
	pConfig->CodeType = Code < 0 ? CODE_TYPE_OFF : CODE_TYPE_CONTINUOUS_TONE;
	pConfig->Code = Code < 0 ? 0U : (uint8_t)Code;
	CAT_Reconfigure();
	return true;
}

static bool CAT_HandleRoleCommand(const char *pCommand, const uint8_t Length, bool *pChanged)
{
	const char RoleChar = pCommand[0];
	const char Field = pCommand[1];
	const uint8_t Role = RoleChar == 'R' ? CAT_ROLE_RX : CAT_ROLE_TX;
	*pChanged = false;

	if (RoleChar == 'T' && Field == 'R')
	{
		if (Length == 2)
		{
			CAT_SendRoleRoute();
		}
		else if ((Length == 6 && memcmp(&pCommand[2], "MAIN", 4) == 0) ||
			(Length == 7 && memcmp(&pCommand[2], "CROSS", 5) == 0))
		{
			if (!CAT_SetRoleRoute(Length == 7))
				CAT_SendRoleError(RoleChar, Field, CAT_ROLE_ERROR_BUSY);
			else
			{
				CAT_SendRoleRoute();
				*pChanged = true;
			}
		}
		else
		{
			CAT_SendRoleError(RoleChar, Field, CAT_ROLE_ERROR_SYNTAX);
		}
		return true;
	}

	if ((RoleChar != 'R' && RoleChar != 'T') || (Field != 'F' && Field != 'M' && Field != 'C'))
		return false;

	if (Field == 'F')
	{
		if (Length == 2)
		{
			CAT_SendRoleFrequency(Role);
			return true;
		}
		uint32_t FrequencyHz;
		if (Length != 11 || !CAT_ParseUnsigned(&pCommand[2], 9, &FrequencyHz))
			CAT_SendRoleError(RoleChar, Field, CAT_ROLE_ERROR_SYNTAX);
		else if (!CAT_CanReconfigure())
			CAT_SendRoleError(RoleChar, Field, CAT_ROLE_ERROR_BUSY);
		else if (RX_freq_check((FrequencyHz + 5U) / 10U) != 0)
			CAT_SendRoleError(RoleChar, Field, CAT_ROLE_ERROR_FREQUENCY);
		else if (!CAT_SetRoleFrequency(Role, FrequencyHz))
			CAT_SendRoleError(RoleChar, Field, CAT_ROLE_ERROR_STATE);
		else
		{
			CAT_SendRoleFrequency(Role);
			*pChanged = true;
		}
		return true;
	}

	if (Field == 'M')
	{
		if (Length == 2)
			CAT_SendRoleMode(Role);
		else if (Length != 4 && Length != 5)
			CAT_SendRoleError(RoleChar, Field, CAT_ROLE_ERROR_SYNTAX);
		else if (!CAT_CanReconfigure())
			CAT_SendRoleError(RoleChar, Field, CAT_ROLE_ERROR_BUSY);
		else if (!CAT_SetRoleMode(Role, &pCommand[2], Length - 2U))
			CAT_SendRoleError(RoleChar, Field, CAT_ROLE_ERROR_UNSUPPORTED);
		else
		{
			CAT_SendRoleMode(Role);
			*pChanged = true;
		}
		return true;
	}

	if (Length == 2)
	{
		CAT_SendRoleCtcss(Role);
	}
	else
	{
		uint32_t Tone;
		if (Length != 6 || !CAT_ParseUnsigned(&pCommand[2], 4, &Tone))
			CAT_SendRoleError(RoleChar, Field, CAT_ROLE_ERROR_SYNTAX);
		else if (!CAT_CanReconfigure())
			CAT_SendRoleError(RoleChar, Field, CAT_ROLE_ERROR_BUSY);
		else if (CAT_FindCtcssCode(Tone) == -2)
			CAT_SendRoleError(RoleChar, Field, CAT_ROLE_ERROR_UNSUPPORTED);
		else if (!CAT_SetRoleCtcss(Role, Tone))
			CAT_SendRoleError(RoleChar, Field, CAT_ROLE_ERROR_STATE);
		else
		{
			CAT_SendRoleCtcss(Role);
			*pChanged = true;
		}
	}
	return true;
}

static bool CAT_IsRemoteInitialized(void)
{
#ifdef ENABLE_REMOTE_CONTROL
	return REMOTE_IsInitialized();
#else
	return false;
#endif
}

static void CAT_HandleCommand(void)
{
	const uint8_t Length = strlen(CAT_Command);
	if (Length == 4U && memcmp(CAT_Command, "RHI3", 4U) == 0)
	{
#ifdef ENABLE_REMOTE_CONTROL
		if (REMOTE_Hello(REMOTE_PROTOCOL_VERSION) != REMOTE_STATUS_OK)
		{
			CAT_SendNamedError("RHI", 3U, CAT_ROLE_ERROR_STATE);
			return;
		}
		static const char Reply[] = "RHI3,128,64,8,8,128,MVLSB,0000001F,UVK5V1,RRCAT3;";
		UART_Send(Reply, sizeof(Reply) - 1U);
		CAT_RecordRoleState();
		CAT_RecordSquelchState();
#else
		CAT_SendNamedError("RHI", 3U, CAT_ROLE_ERROR_UNSUPPORTED);
#endif
		return;
	}
	if (Length >= 3U && memcmp(CAT_Command, "RHI", 3U) == 0)
	{
		CAT_SendNamedError("RHI", 3U,
			Length == 4U && CAT_Command[3] >= '0' && CAT_Command[3] <= '9'
				? CAT_ROLE_ERROR_UNSUPPORTED : CAT_ROLE_ERROR_SYNTAX);
		return;
	}

	if (Length < 2)
	{
		CAT_SendError();
		return;
	}

	if (CAT_Command[0] == 'S' && CAT_Command[1] == 'Q')
	{
		if (!CAT_IsRemoteInitialized())
		{
			CAT_SendNamedError("SQ", 2U, CAT_ROLE_ERROR_STATE);
			return;
		}
		if (Length == 2U)
		{
			CAT_SendSquelch();
			CAT_RecordSquelchState();
			return;
		}
		if (Length != 3U || CAT_Command[2] < '0' || CAT_Command[2] > '9')
		{
			CAT_SendNamedError("SQ", 2U, CAT_ROLE_ERROR_SYNTAX);
			return;
		}
		if (!CAT_CanReconfigure())
		{
			CAT_SendNamedError("SQ", 2U, CAT_ROLE_ERROR_BUSY);
			return;
		}

		const uint8_t SquelchLevel = CAT_Command[2] - '0';
		if (gEeprom.SQUELCH_LEVEL != SquelchLevel)
		{
			gEeprom.SQUELCH_LEVEL = SquelchLevel;
			RADIO_ConfigureSquelchAndOutputPower(&gEeprom.VfoInfo[0]);
			RADIO_ConfigureSquelchAndOutputPower(&gEeprom.VfoInfo[1]);
			gFlagReconfigureVfos = false;
			CAT_Reconfigure();
			SETTINGS_SaveSettings();
			gRequestSaveSettings = false;
		}
		CAT_RecordSquelchState();
		CAT_SendSquelch();
		return;
	}

	if (CAT_Command[0] == 'A' && CAT_Command[1] == 'B')
	{
		if (Length != 2U)
		{
			CAT_SendNamedError("AB", 2U, CAT_ROLE_ERROR_SYNTAX);
			return;
		}
		if (!CAT_IsRemoteInitialized())
		{
			CAT_SendNamedError("AB", 2U, CAT_ROLE_ERROR_STATE);
			return;
		}
		if (!CAT_CanReconfigure())
		{
			CAT_SendNamedError("AB", 2U, CAT_ROLE_ERROR_BUSY);
			return;
		}

		char Reply[CAT_ROLE_ALL_MAX_LENGTH + 1U];
		const bool Cross = CAT_IsRoleCrossBand();
		const uint8_t ReplyLength = gEeprom.DUAL_WATCH == DUAL_WATCH_OFF
			? CAT_BuildRoleAllForSelection(Reply, Cross, gEeprom.TX_VFO ^ 1U) : 0U;
		if (ReplyLength == 0U)
		{
			CAT_SendNamedError("AB", 2U, CAT_ROLE_ERROR_STATE);
			return;
		}

		COMMON_SwitchVFOs();
		gFlagReconfigureVfos = false;
		CAT_Reconfigure();
		gVFO_RSSI_bar_level[0] = 0;
		gVFO_RSSI_bar_level[1] = 0;
		SETTINGS_SaveSettings();
		gRequestSaveSettings = false;
		GUI_SelectNextDisplay(DISPLAY_MAIN);
		gRequestDisplayScreen = DISPLAY_INVALID;
		UART_Send(Reply, ReplyLength);
		CAT_RecordRoleState();
		return;
	}

	if (CAT_Command[0] == 'P' && CAT_Command[1] == 'T')
	{
		if (!CAT_IsRemoteInitialized())
		{
			CAT_SendNamedError("PT", 2U, CAT_ROLE_ERROR_STATE);
			return;
		}
#ifdef ENABLE_REMOTE_CONTROL
		if (Length == 2U)
		{
			const char Reply[] = {'P', 'T', REMOTE_IsPttActive() ? '1' : '0', ';'};
			UART_Send(Reply, sizeof(Reply));
			return;
		}
		if (Length != 3U || (CAT_Command[2] != '0' && CAT_Command[2] != '1'))
		{
			CAT_SendNamedError("PT", 2U, CAT_ROLE_ERROR_SYNTAX);
			return;
		}
		if (REMOTE_SetPtt(CAT_Command[2] == '1') != REMOTE_STATUS_OK)
		{
			CAT_SendNamedError("PT", 2U, CAT_ROLE_ERROR_BUSY);
			return;
		}
		const char Reply[] = {'P', 'T', REMOTE_IsPttActive() ? '1' : '0', ';'};
		UART_Send(Reply, sizeof(Reply));
#endif
		return;
	}

	if (Length >= 3U && memcmp(CAT_Command, "TRA", 3U) == 0)
	{
		if (!CAT_IsRemoteInitialized())
			CAT_SendRoleAllError(CAT_ROLE_ERROR_STATE);
		else
			CAT_HandleRoleAllCommand(CAT_Command, Length);
		return;
	}

	if ((CAT_Command[0] == 'R' || CAT_Command[0] == 'T') && Length >= 2U)
	{
		if (!CAT_IsRemoteInitialized())
		{
			CAT_SendRoleError(CAT_Command[0], CAT_Command[1], CAT_ROLE_ERROR_STATE);
			return;
		}
		bool RoleChanged;
		if (CAT_HandleRoleCommand(CAT_Command, Length, &RoleChanged))
		{
			if (RoleChanged)
				CAT_RecordRoleState();
			return;
		}
	}

	CAT_SendError();
	return;

}

static void SendReply(void *pReply, uint16_t Size)
{
	Header_t Header;
	Footer_t Footer;

	if (bIsEncrypted)
	{
		uint8_t     *pBytes = (uint8_t *)pReply;
		unsigned int i;
		for (i = 0; i < Size; i++)
			pBytes[i] ^= Obfuscation[i % 16];
	}

	Header.ID = 0xCDAB;
	Header.Size = Size;
	UART_Send(&Header, sizeof(Header));
	UART_Send(pReply, Size);

	if (bIsEncrypted)
	{
		Footer.Padding[0] = Obfuscation[(Size + 0) % 16] ^ 0xFF;
		Footer.Padding[1] = Obfuscation[(Size + 1) % 16] ^ 0xFF;
	}
	else
	{
		Footer.Padding[0] = 0xFF;
		Footer.Padding[1] = 0xFF;
	}
	Footer.ID = 0xBADC;

	UART_Send(&Footer, sizeof(Footer));
}

#ifdef ENABLE_REMOTE_CONTROL
static void SendRemoteReply(const void *pReply, const uint16_t Size)
{
	Header_t Header;
	const uint16_t Crc = CRC_Calculate(pReply, Size);
	const uint16_t Footer = 0xBADC;

	Header.ID = 0xCDAB;
	Header.Size = Size;
	UART_Send(&Header, sizeof(Header));
	UART_Send(pReply, Size);
	UART_Send(&Crc, sizeof(Crc));
	UART_Send(&Footer, sizeof(Footer));
}

static bool RemoteCommandHasLength(const uint16_t PayloadSize)
{
	return gBinaryCommandSize == (uint16_t)(sizeof(Header_t) + PayloadSize) &&
		UART_Command.Header.Size == PayloadSize;
}

static void CMD_REMOTE_DISPLAY_SUBSCRIBE(const uint8_t *pBuffer)
{
	const CMD_REMOTE_DISPLAY_SUBSCRIBE_t *pCmd = (const CMD_REMOTE_DISPLAY_SUBSCRIBE_t *)pBuffer;
	REPLY_REMOTE_DISPLAY_SUBSCRIBE_t Reply;
	uint16_t PageRevision[REMOTE_DISPLAY_PAGE_COUNT] = {0};

	memset(&Reply, 0, sizeof(Reply));
	Reply.Header.ID = REMOTE_REPLY_DISPLAY_SUBSCRIBE;
	Reply.Header.Size = sizeof(Reply.Data);
	if (!RemoteCommandHasLength(1U) || pCmd->Enabled > 1U)
		Reply.Data.Status = REMOTE_STATUS_BAD_LENGTH;
	else
		Reply.Data.Status = REMOTE_SetDisplaySubscribed(pCmd->Enabled != 0,
			&Reply.Data.DirtyMask, PageRevision);
	memcpy(Reply.Data.PageRevision, PageRevision, sizeof(PageRevision));
	Reply.Data.Enabled = pCmd->Enabled != 0 && Reply.Data.Status == REMOTE_STATUS_OK;
	SendRemoteReply(&Reply, sizeof(Reply));
}

static void CMD_REMOTE_DISPLAY_PAGE(const uint8_t *pBuffer)
{
	const CMD_REMOTE_DISPLAY_PAGE_t *pCmd = (const CMD_REMOTE_DISPLAY_PAGE_t *)pBuffer;
	REPLY_REMOTE_DISPLAY_PAGE_t Reply;

	memset(&Reply, 0, sizeof(Reply));
	Reply.Header.ID = REMOTE_REPLY_DISPLAY_PAGE;
	Reply.Header.Size = sizeof(Reply.Data);
	if (RemoteCommandHasLength(1U))
	{
		Reply.Data.Page = pCmd->Page;
		Reply.Data.Status = REMOTE_GetDisplayPage(pCmd->Page, Reply.Data.Pixels,
			&Reply.Data.Revision, &Reply.Data.Crc);
	}
	else
		Reply.Data.Status = REMOTE_STATUS_BAD_LENGTH;

	SendRemoteReply(&Reply, sizeof(Reply));
}

static void CMD_REMOTE_KEY_EVENT(const uint8_t *pBuffer)
{
	const CMD_REMOTE_KEY_EVENT_t *pCmd = (const CMD_REMOTE_KEY_EVENT_t *)pBuffer;
	REPLY_REMOTE_KEY_EVENT_t Reply;

	memset(&Reply, 0, sizeof(Reply));
	Reply.Header.ID = REMOTE_REPLY_KEY_EVENT;
	Reply.Header.Size = sizeof(Reply.Data);
	if (RemoteCommandHasLength(2U))
	{
		Reply.Data.Key = pCmd->Key;
		Reply.Data.Action = pCmd->Action;
		Reply.Data.Status = REMOTE_KeyEvent((KEY_Code_t)pCmd->Key, (REMOTE_KeyAction_t)pCmd->Action);
	}
	else
		Reply.Data.Status = REMOTE_STATUS_BAD_LENGTH;

	SendRemoteReply(&Reply, sizeof(Reply));
}

static void CMD_REMOTE_RELEASE_ALL(void)
{
	REPLY_REMOTE_RELEASE_ALL_t Reply;

	memset(&Reply, 0, sizeof(Reply));
	Reply.Header.ID = REMOTE_REPLY_RELEASE_ALL;
	Reply.Header.Size = sizeof(Reply.Data);
	if (!RemoteCommandHasLength(0U))
		Reply.Data.Status = REMOTE_STATUS_BAD_LENGTH;
	else if (!CAT_IsRemoteInitialized())
		Reply.Data.Status = REMOTE_STATUS_BAD_STATE;
	else
	{
		REMOTE_ReleaseKeys();
		Reply.Data.Status = REMOTE_STATUS_OK;
	}

	SendRemoteReply(&Reply, sizeof(Reply));
}

void UART_NotifyRemoteDisplayChanges(void)
{
	bool PttActive;
	if (REMOTE_TakePttChange(&PttActive))
	{
		const char Reply[] = {'P', 'T', PttActive ? '1' : '0', ';'};
		UART_Send(Reply, sizeof(Reply));
	}

	EVENT_REMOTE_DISPLAY_CHANGED_t Event;
	uint16_t PageRevision[REMOTE_DISPLAY_PAGE_COUNT];

	memset(&Event, 0, sizeof(Event));
	if (!REMOTE_TakeDisplayChange(&Event.Data.DirtyMask, PageRevision))
		return;
	memcpy(Event.Data.PageRevision, PageRevision, sizeof(PageRevision));

	Event.Header.ID = REMOTE_EVENT_DISPLAY_CHANGED;
	Event.Header.Size = sizeof(Event.Data);
	SendRemoteReply(&Event, sizeof(Event));
}
#endif

static void SendVersion(void)
{
	REPLY_0514_t Reply;

	Reply.Header.ID = 0x0515;
	Reply.Header.Size = sizeof(Reply.Data);
	strcpy(Reply.Data.Version, Version);
	Reply.Data.bHasCustomAesKey = bHasCustomAesKey;
	Reply.Data.bIsInLockScreen = bIsInLockScreen;
	Reply.Data.Challenge[0] = gChallenge[0];
	Reply.Data.Challenge[1] = gChallenge[1];
	Reply.Data.Challenge[2] = gChallenge[2];
	Reply.Data.Challenge[3] = gChallenge[3];

	SendReply(&Reply, sizeof(Reply));
}

static bool IsBadChallenge(const uint32_t *pKey, const uint32_t *pIn, const uint32_t *pResponse)
{
	unsigned int i;
	uint32_t     IV[4];

	IV[0] = 0;
	IV[1] = 0;
	IV[2] = 0;
	IV[3] = 0;

	AES_Encrypt(pKey, IV, pIn, IV, true);

	for (i = 0; i < 4; i++)
		if (IV[i] != pResponse[i])
			return true;

	return false;
}

// session init, sends back version info and state
// timestamp is a session id really
static void CMD_0514(const uint8_t *pBuffer)
{
	const CMD_0514_t *pCmd = (const CMD_0514_t *)pBuffer;

	Timestamp = pCmd->Timestamp;

	#ifdef ENABLE_FMRADIO
		gFmRadioCountdown_500ms = fm_radio_countdown_500ms;
	#endif

	gSerialConfigCountDown_500ms = 12; // 6 sec
	
	// turn the LCD backlight off
	BACKLIGHT_TurnOff();

	SendVersion();
}

// read eeprom
static void CMD_051B(const uint8_t *pBuffer)
{
	const CMD_051B_t *pCmd = (const CMD_051B_t *)pBuffer;
	REPLY_051B_t      Reply;
	bool              bLocked = false;

	if (pCmd->Timestamp != Timestamp)
		return;

	gSerialConfigCountDown_500ms = 12; // 6 sec

	#ifdef ENABLE_FMRADIO
		gFmRadioCountdown_500ms = fm_radio_countdown_500ms;
	#endif

	memset(&Reply, 0, sizeof(Reply));
	Reply.Header.ID   = 0x051C;
	Reply.Header.Size = pCmd->Size + 4;
	Reply.Data.Offset = pCmd->Offset;
	Reply.Data.Size   = pCmd->Size;

	if (bHasCustomAesKey)
		bLocked = gIsLocked;

	if (!bLocked)
		EEPROM_ReadBuffer(pCmd->Offset, Reply.Data.Data, pCmd->Size);

	SendReply(&Reply, pCmd->Size + 8);
}

// write eeprom
static void CMD_051D(const uint8_t *pBuffer)
{
	const CMD_051D_t *pCmd = (const CMD_051D_t *)pBuffer;
	REPLY_051D_t Reply;
	bool bReloadEeprom;
	bool bIsLocked;

	if (pCmd->Timestamp != Timestamp)
		return;

	gSerialConfigCountDown_500ms = 12; // 6 sec
	
	bReloadEeprom = false;

	#ifdef ENABLE_FMRADIO
		gFmRadioCountdown_500ms = fm_radio_countdown_500ms;
	#endif

	Reply.Header.ID   = 0x051E;
	Reply.Header.Size = sizeof(Reply.Data);
	Reply.Data.Offset = pCmd->Offset;

	bIsLocked = bHasCustomAesKey ? gIsLocked : bHasCustomAesKey;

	if (!bIsLocked)
	{
		unsigned int i;
		for (i = 0; i < (pCmd->Size / 8); i++)
		{
			const uint16_t Offset = pCmd->Offset + (i * 8U);

			if (Offset >= 0x0F30 && Offset < 0x0F40)
				if (!gIsLocked)
					bReloadEeprom = true;

			if ((Offset < 0x0E98 || Offset >= 0x0EA0) || !bIsInLockScreen || pCmd->bAllowPassword)
				EEPROM_WriteBuffer(Offset, &pCmd->Data[i * 8U]);
		}

		if (bReloadEeprom)
			SETTINGS_InitEEPROM();
	}

	SendReply(&Reply, sizeof(Reply));
}

// read RSSI
static void CMD_0527(void)
{
	REPLY_0527_t Reply;

	Reply.Header.ID             = 0x0528;
	Reply.Header.Size           = sizeof(Reply.Data);
	Reply.Data.RSSI             = BK4819_ReadRegister(BK4819_REG_67) & 0x01FF;
	Reply.Data.ExNoiseIndicator = BK4819_ReadRegister(BK4819_REG_65) & 0x007F;
	Reply.Data.GlitchIndicator  = BK4819_ReadRegister(BK4819_REG_63);

	SendReply(&Reply, sizeof(Reply));
}

// read ADC
static void CMD_0529(void)
{
	REPLY_0529_t Reply;

	Reply.Header.ID   = 0x52A;
	Reply.Header.Size = sizeof(Reply.Data);

	// Original doesn't actually send current!
	BOARD_ADC_GetBatteryInfo(&Reply.Data.Voltage, &Reply.Data.Current);

	SendReply(&Reply, sizeof(Reply));
}

static void CMD_052D(const uint8_t *pBuffer)
{
	const CMD_052D_t *pCmd = (const CMD_052D_t *)pBuffer;
	REPLY_052D_t      Reply;
	bool              bIsLocked;

	#ifdef ENABLE_FMRADIO
		gFmRadioCountdown_500ms = fm_radio_countdown_500ms;
	#endif
	Reply.Header.ID   = 0x052E;
	Reply.Header.Size = sizeof(Reply.Data);

	bIsLocked = bHasCustomAesKey;

	if (!bIsLocked)
		bIsLocked = IsBadChallenge(gCustomAesKey, gChallenge, pCmd->Response);

	if (!bIsLocked)
	{
		bIsLocked = IsBadChallenge(gDefaultAesKey, gChallenge, pCmd->Response);
		if (bIsLocked)
			gTryCount++;
	}

	if (gTryCount < 3)
	{
		if (!bIsLocked)
			gTryCount = 0;
	}
	else
	{
		gTryCount = 3;
		bIsLocked = true;
	}
	
	gIsLocked            = bIsLocked;
	Reply.Data.bIsLocked = bIsLocked;

	SendReply(&Reply, sizeof(Reply));
}

// session init, sends back version info and state
// timestamp is a session id really
// this command also disables dual watch, crossband, 
// DTMF side tones, freq reverse, PTT ID, DTMF decoding, frequency offset
// exits power save, sets main VFO to upper,
static void CMD_052F(const uint8_t *pBuffer)
{
	const CMD_052F_t *pCmd = (const CMD_052F_t *)pBuffer;

	gEeprom.DUAL_WATCH                               = DUAL_WATCH_OFF;
	gEeprom.CROSS_BAND_RX_TX                         = CROSS_BAND_OFF;
	gEeprom.RX_VFO                                   = 0;
	gEeprom.DTMF_SIDE_TONE                           = false;
	gEeprom.VfoInfo[0].FrequencyReverse              = false;
	gEeprom.VfoInfo[0].pRX                           = &gEeprom.VfoInfo[0].freq_config_RX;
	gEeprom.VfoInfo[0].pTX                           = &gEeprom.VfoInfo[0].freq_config_TX;
	gEeprom.VfoInfo[0].TX_OFFSET_FREQUENCY_DIRECTION = TX_OFFSET_FREQUENCY_DIRECTION_OFF;
	gEeprom.VfoInfo[0].DTMF_PTT_ID_TX_MODE           = PTT_ID_OFF;
#ifdef ENABLE_DTMF_CALLING
	gEeprom.VfoInfo[0].DTMF_DECODING_ENABLE          = false;
#endif

	#ifdef ENABLE_NOAA
		gIsNoaaMode = false;
	#endif

	if (gCurrentFunction == FUNCTION_POWER_SAVE)
		FUNCTION_Select(FUNCTION_FOREGROUND);

	gSerialConfigCountDown_500ms = 12; // 6 sec

	Timestamp = pCmd->Timestamp;

	// turn the LCD backlight off
	BACKLIGHT_TurnOff();

	SendVersion();
}

#ifdef ENABLE_UART_RW_BK_REGS
static void CMD_0601_ReadBK4819Reg(const uint8_t *pBuffer)
{
	typedef struct  __attribute__((__packed__)) {
		Header_t header;
		uint8_t reg;
	} CMD_0601_t;

	CMD_0601_t *cmd = (CMD_0601_t*) pBuffer;

	struct __attribute__((__packed__)) {
		Header_t header;
		struct __attribute__((__packed__)) {
			uint8_t reg;
			uint16_t value;
		} data;
	} reply;

	reply.header.ID = 0x0601;
	reply.header.Size = sizeof(reply.data);
	reply.data.reg = cmd->reg;
	reply.data.value = BK4819_ReadRegister(cmd->reg);
	SendReply(&reply, sizeof(reply));
}

static void CMD_0602_WriteBK4819Reg(const uint8_t *pBuffer)
{
	typedef struct __attribute__((__packed__)) {
		Header_t header;
		uint8_t reg;
		uint16_t value;
	} CMD_0602_t;

	CMD_0602_t *cmd = (CMD_0602_t*) pBuffer;
	BK4819_WriteRegister(cmd->reg, cmd->value);
}
#endif

#ifdef ENABLE_REMOTE_CONTROL
static bool UART_IsRemoteCommandId(const uint16_t Id)
{
	return Id == REMOTE_COMMAND_DISPLAY_SUBSCRIBE ||
		Id == REMOTE_COMMAND_DISPLAY_PAGE ||
		Id == REMOTE_COMMAND_KEY_EVENT ||
		Id == REMOTE_COMMAND_RELEASE_ALL;
}
#endif

static bool UART_IsBinaryCommandAvailable(uint16_t DmaLength)
{
	uint16_t Index;
	uint16_t TailIndex;
	uint16_t Size;
	uint16_t CRC;
	uint16_t CommandLength;
#ifdef ENABLE_REMOTE_CONTROL
	bool IsRemoteCommand;
#endif
	if (gUART_WriteIndex < DmaLength)
		CommandLength = DmaLength - gUART_WriteIndex;
	else
		CommandLength = (DmaLength + sizeof(UART_DMA_Buffer)) - gUART_WriteIndex;

	if (CommandLength < 8)
		return false;

	Index = DMA_INDEX(gUART_WriteIndex, 2);
	Size  = (UART_DMA_Buffer[DMA_INDEX(Index, 1)] << 8) | UART_DMA_Buffer[Index];

	if ((Size + 8u) > sizeof(UART_DMA_Buffer))
	{
		UART_ConsumeBytes(1);
		return false;
	}

	if (CommandLength < (Size + 8))
		return false;

	Index     = DMA_INDEX(Index, 2);
	TailIndex = DMA_INDEX(Index, Size + 2);

	if (UART_DMA_Buffer[TailIndex] != 0xDC || UART_DMA_Buffer[DMA_INDEX(TailIndex, 1)] != 0xBA)
	{
		UART_ConsumeBytes(1);
		return false;
	}

	if (TailIndex < Index)
	{
		const uint16_t ChunkSize = sizeof(UART_DMA_Buffer) - Index;
		memcpy(UART_Command.Buffer, UART_DMA_Buffer + Index, ChunkSize);
		memcpy(UART_Command.Buffer + ChunkSize, UART_DMA_Buffer, TailIndex);
	}
	else
		memcpy(UART_Command.Buffer, UART_DMA_Buffer + Index, TailIndex - Index);

	TailIndex = DMA_INDEX(TailIndex, 2);
	if (TailIndex < gUART_WriteIndex)
	{
		memset(UART_DMA_Buffer + gUART_WriteIndex, 0, sizeof(UART_DMA_Buffer) - gUART_WriteIndex);
		memset(UART_DMA_Buffer, 0, TailIndex);
	}
	else
		memset(UART_DMA_Buffer + gUART_WriteIndex, 0, TailIndex - gUART_WriteIndex);

	gUART_WriteIndex = TailIndex;

#ifdef ENABLE_REMOTE_CONTROL
	IsRemoteCommand = UART_IsRemoteCommandId(UART_Command.Header.ID);
#endif

	if (UART_Command.Header.ID == 0x0514)
		bIsEncrypted = false;

	if (UART_Command.Header.ID == 0x6902)
		bIsEncrypted = true;

	if (bIsEncrypted
#ifdef ENABLE_REMOTE_CONTROL
		&& !IsRemoteCommand
#endif
	)
	{
		unsigned int i;
		for (i = 0; i < (Size + 2u); i++)
			UART_Command.Buffer[i] ^= Obfuscation[i % 16];
	}
	
	CRC = UART_Command.Buffer[Size] | (UART_Command.Buffer[Size + 1] << 8);

	if (CRC_Calculate(UART_Command.Buffer, Size) != CRC)
		return false;

	gBinaryCommandSize = Size;
	UART_CommandType = UART_COMMAND_BINARY;
	return true;
}

bool UART_IsCommandAvailable(void)
{
	const uint16_t DmaLength = DMA_CH0->ST & 0xFFFU;

	while (gUART_WriteIndex != DmaLength)
	{
		const uint8_t First = UART_DMA_Buffer[gUART_WriteIndex];

		if (gATActive)
		{
			// Reuse the binary command buffer, preserving AT bytes verbatim.
			gParserLastInputPosition = DmaLength;
			gParserTimeout_10ms = SERIAL_INCOMPLETE_TIMEOUT_10MS;
			const bool Complete = UART_AT_Push(&gATParser, UART_Command.Buffer, First);
			UART_ConsumeBytes(1U);
			if (Complete)
			{
				gATActive = false;
				if (!gATParser.Overflow)
				{
					gATCommandSize = gATParser.Length;
					UART_CommandType = UART_COMMAND_AT;
					return true;
				}
			}
			continue;
		}

		if (First == 'A')
		{
			if (DMA_INDEX(gUART_WriteIndex, 1) == DmaLength)
				return false;
			if (UART_DMA_Buffer[DMA_INDEX(gUART_WriteIndex, 1)] == 'T')
			{
				memset(&gATParser, 0, sizeof(gATParser));
				gATActive = true;
				continue;
			}
		}

		if (First == 0xABU)
		{
			if (DMA_INDEX(gUART_WriteIndex, 1) == DmaLength)
				return false;

			if (UART_DMA_Buffer[DMA_INDEX(gUART_WriteIndex, 1)] == 0xCDU)
				return UART_IsBinaryCommandAvailable(DmaLength);

			UART_ConsumeBytes(1);
			continue;
		}

		if (!((First >= 'A' && First <= 'Z') || (First >= 'a' && First <= 'z')))
		{
			UART_ConsumeBytes(1);
			continue;
		}

		uint16_t Index = gUART_WriteIndex;
		uint16_t Length = 0;
		bool Invalid = false;

		while (Index != DmaLength)
		{
			uint8_t Byte = UART_DMA_Buffer[Index];
			const uint16_t NextIndex = DMA_INDEX(Index, 1);
			if (Byte == 0xABU && NextIndex != DmaLength && UART_DMA_Buffer[NextIndex] == 0xCDU)
			{
				UART_ConsumeBytes(Length);
				return UART_IsCommandAvailable();
			}

			if (Byte == ';')
			{
				const bool IsRoleAll = Length >= 3U &&
					CAT_Command[0] == 'T' && CAT_Command[1] == 'R' && CAT_Command[2] == 'A';
				const bool IsHandshake = Length >= 3U &&
					CAT_Command[0] == 'R' && CAT_Command[1] == 'H' && CAT_Command[2] == 'I';
				const bool IsPtt = Length >= 2U && CAT_Command[0] == 'P' && CAT_Command[1] == 'T';
				UART_ConsumeBytes(Length + 1U);
				if (Length < 2U || Length > CAT_COMMAND_MAX_LENGTH || Invalid)
				{
					if (IsRoleAll)
						CAT_SendRoleAllError(CAT_ROLE_ERROR_SYNTAX);
					else if (IsHandshake)
						CAT_SendNamedError("RHI", 3U, CAT_ROLE_ERROR_SYNTAX);
					else if (IsPtt)
						CAT_SendNamedError("PT", 2U, CAT_ROLE_ERROR_SYNTAX);
					break;
				}

				CAT_Command[Length] = 0;
				UART_CommandType = UART_COMMAND_CAT;
				return true;
			}

			if (!((Byte >= 'A' && Byte <= 'Z') || (Byte >= 'a' && Byte <= 'z') ||
				(Byte >= '0' && Byte <= '9') || Byte == ','))
				Invalid = true;

			if (Byte >= 'a' && Byte <= 'z')
				Byte -= 'a' - 'A';
			if (Length < CAT_COMMAND_MAX_LENGTH)
				CAT_Command[Length] = Byte;
			Length++;
			Index = DMA_INDEX(Index, 1);
		}

		if (Index == DmaLength)
			return false;
	}

	return false;
}

void UART_TimeSlice10ms(void)
{
	const uint16_t DmaLength = DMA_CH0->ST & 0xFFFU;
	if (gUART_WriteIndex == DmaLength && !gATActive)
	{
		gParserTimeout_10ms = 0;
	}
	else if (gParserLastInputPosition != DmaLength)
	{
		gParserLastInputPosition = DmaLength;
		gParserTimeout_10ms = SERIAL_INCOMPLETE_TIMEOUT_10MS;
	}
	else if (gParserTimeout_10ms > 0U && --gParserTimeout_10ms == 0U)
	{
		while (gUART_WriteIndex != DmaLength)
			UART_ConsumeBytes(1U);
		gATActive = false;
		UART_CommandType = UART_COMMAND_NONE;
	}
}

void UART_HandleCommand(void)
{
	if (UART_CommandType == UART_COMMAND_AT)
	{
		UART_CommandType = UART_COMMAND_NONE;
		UART_Send(UART_Command.Buffer, gATCommandSize);
		return;
	}

	if (UART_CommandType == UART_COMMAND_CAT)
	{
		UART_CommandType = UART_COMMAND_NONE;
		CAT_HandleCommand();
		return;
	}

	UART_CommandType = UART_COMMAND_NONE;
	switch (UART_Command.Header.ID)
	{
		case 0x0514:
			CMD_0514(UART_Command.Buffer);
			break;
	
		case 0x051B:
			CMD_051B(UART_Command.Buffer);
			break;
	
		case 0x051D:
			CMD_051D(UART_Command.Buffer);
			break;
	
		case 0x051F:	// Not implementing non-authentic command
			break;
	
		case 0x0521:	// Not implementing non-authentic command
			break;
	
		case 0x0527:
			CMD_0527();
			break;
	
		case 0x0529:
			CMD_0529();
			break;
	
		case 0x052D:
			CMD_052D(UART_Command.Buffer);
			break;
	
		case 0x052F:
			CMD_052F(UART_Command.Buffer);
			break;
	
		case 0x05DD: // reset
			#if defined(ENABLE_OVERLAY)
				overlay_FLASH_RebootToBootloader();
			#else
				NVIC_SystemReset();
			#endif
			break;
			
#ifdef ENABLE_UART_RW_BK_REGS
		case 0x0601:
			CMD_0601_ReadBK4819Reg(UART_Command.Buffer);
			break;
		
		case 0x0602:
			CMD_0602_WriteBK4819Reg(UART_Command.Buffer);
			break;
#endif

#ifdef ENABLE_REMOTE_CONTROL
		case REMOTE_COMMAND_DISPLAY_SUBSCRIBE:
			CMD_REMOTE_DISPLAY_SUBSCRIBE(UART_Command.Buffer);
			break;

		case REMOTE_COMMAND_DISPLAY_PAGE:
			CMD_REMOTE_DISPLAY_PAGE(UART_Command.Buffer);
			break;

		case REMOTE_COMMAND_KEY_EVENT:
			CMD_REMOTE_KEY_EVENT(UART_Command.Buffer);
			break;

		case REMOTE_COMMAND_RELEASE_ALL:
			CMD_REMOTE_RELEASE_ALL();
			break;
#endif
	}
}
