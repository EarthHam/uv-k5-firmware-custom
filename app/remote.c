/* Copyright 2026
 * Licensed under the Apache License, Version 2.0.
 */

#include <string.h>

#include "app/app.h"
#include "app/remote.h"
#include "driver/crc.h"
#include "driver/st7565.h"
#include "misc.h"

#define REMOTE_KEY_LEASE_10MS ((1000U / 10U) + 1U)
#define REMOTE_DISPLAY_NOTIFY_INTERVAL_10MS 2U

typedef struct {
	KEY_Code_t Key;
	uint16_t   Lease_10ms;
	uint16_t   PressTime_10ms;
	bool       Held;
} REMOTE_KeyState_t;

static bool              gRemoteInitialized;
static bool              gRemoteDisplaySubscribed;
static bool              gRemotePttDirty;
static bool              gRemotePttPressed;
static uint16_t          gRemotePttLease_10ms;
static REMOTE_KeyState_t gRemoteKey;
static uint16_t          gRemotePageCrc[REMOTE_DISPLAY_PAGE_COUNT];
static uint16_t          gRemotePageRevision[REMOTE_DISPLAY_PAGE_COUNT];
static uint8_t           gRemoteDirtyPages;
static uint8_t           gRemoteDisplayNotifyCooldown_10ms;
static bool              gRemoteLastPttActive;

static void REMOTE_RecordPttChange(const bool NotifySession)
{
	const bool Active = REMOTE_IsPttActive();
	if (Active == gRemoteLastPttActive)
		return;
	gRemoteLastPttActive = Active;
	if (NotifySession && gRemoteInitialized)
		gRemotePttDirty = true;
}

static const uint8_t *REMOTE_GetPageBuffer(const uint8_t Page)
{
	if (Page == 0)
		return gStatusLine;

	return gFrameBuffer[Page - 1U];
}

static void REMOTE_ReleaseKey(void)
{
	if (gRemoteKey.Key == KEY_INVALID)
		return;

	APP_ProcessKey(gRemoteKey.Key, false, gRemoteKey.Held);
	gRemoteKey.Key            = KEY_INVALID;
	gRemoteKey.Lease_10ms     = 0;
	gRemoteKey.PressTime_10ms = 0;
	gRemoteKey.Held           = false;
}

static void REMOTE_ReleasePtt(void)
{
	if (!gRemotePttPressed)
		return;

	APP_ProcessKey(KEY_PTT, false, false);
	gRemotePttPressed    = false;
	gRemotePttLease_10ms = 0;
}

void REMOTE_Init(void)
{
	gRemoteInitialized = false;
	gRemoteDisplaySubscribed = false;
	gRemotePttDirty = false;
	gRemotePttPressed = false;
	gRemotePttLease_10ms = 0;
	gRemoteKey.Key = KEY_INVALID;
	gRemoteKey.Lease_10ms = 0;
	gRemoteKey.PressTime_10ms = 0;
	gRemoteKey.Held = false;
	gRemoteDirtyPages = 0;
	gRemoteDisplayNotifyCooldown_10ms = 0;
	gRemoteLastPttActive = gCurrentFunction == FUNCTION_TRANSMIT;
	for (uint8_t Page = 0; Page < REMOTE_DISPLAY_PAGE_COUNT; Page++)
	{
		gRemotePageCrc[Page] = CRC_Calculate(REMOTE_GetPageBuffer(Page), REMOTE_DISPLAY_PAGE_SIZE);
		gRemotePageRevision[Page] = 1U;
	}
}

REMOTE_Status_t REMOTE_Hello(const uint8_t ProtocolVersion)
{
	REMOTE_ReleaseAll();
	gRemoteLastPttActive = REMOTE_IsPttActive();
	gRemoteDisplaySubscribed = false;
	gRemotePttDirty = false;
	gRemoteDirtyPages = 0;
	gRemoteInitialized = ProtocolVersion == REMOTE_PROTOCOL_VERSION;
	return gRemoteInitialized ? REMOTE_STATUS_OK : REMOTE_STATUS_BAD_VERSION;
}

bool REMOTE_IsInitialized(void)
{
	return gRemoteInitialized;
}

REMOTE_Status_t REMOTE_SetDisplaySubscribed(const bool Enabled, uint8_t *pDirtyMask,
	uint16_t PageRevision[REMOTE_DISPLAY_PAGE_COUNT])
{
	if (!gRemoteInitialized)
		return REMOTE_STATUS_BAD_STATE;

	gRemoteDisplaySubscribed = Enabled;
	gRemoteDirtyPages = 0;
	*pDirtyMask = Enabled ? 0xFFU : 0U;
	for (uint8_t Page = 0; Page < REMOTE_DISPLAY_PAGE_COUNT; Page++)
		PageRevision[Page] = gRemotePageRevision[Page];
	return REMOTE_STATUS_OK;
}

REMOTE_Status_t REMOTE_GetDisplayPage(const uint8_t Page, uint8_t Data[REMOTE_DISPLAY_PAGE_SIZE],
	uint16_t *pRevision, uint16_t *pCrc)
{
	if (!gRemoteInitialized)
		return REMOTE_STATUS_BAD_STATE;
	if (Page >= REMOTE_DISPLAY_PAGE_COUNT)
		return REMOTE_STATUS_BAD_PAGE;

	const uint8_t *pPage = REMOTE_GetPageBuffer(Page);
	memcpy(Data, pPage, REMOTE_DISPLAY_PAGE_SIZE);
	*pRevision = gRemotePageRevision[Page];
	*pCrc = CRC_Calculate(Data, REMOTE_DISPLAY_PAGE_SIZE);
	return REMOTE_STATUS_OK;
}

void REMOTE_MarkDisplayPage(const uint8_t Page)
{
	if (Page >= REMOTE_DISPLAY_PAGE_COUNT)
		return;

	const uint16_t Crc = CRC_Calculate(REMOTE_GetPageBuffer(Page), REMOTE_DISPLAY_PAGE_SIZE);
	if (Crc == gRemotePageCrc[Page])
		return;

	gRemotePageCrc[Page] = Crc;
	gRemotePageRevision[Page]++;
	if (gRemoteDisplaySubscribed)
		gRemoteDirtyPages |= 1U << Page;
}

void REMOTE_ForceDisplayRefresh(void)
{
	for (uint8_t Page = 0; Page < REMOTE_DISPLAY_PAGE_COUNT; Page++)
	{
		gRemotePageCrc[Page] = CRC_Calculate(REMOTE_GetPageBuffer(Page), REMOTE_DISPLAY_PAGE_SIZE);
		gRemotePageRevision[Page]++;
	}
	if (gRemoteDisplaySubscribed)
		gRemoteDirtyPages = 0xFFU;
}

bool REMOTE_TakeDisplayChange(uint8_t *pDirtyMask,
	uint16_t PageRevision[REMOTE_DISPLAY_PAGE_COUNT])
{
	if (!gRemoteInitialized || !gRemoteDisplaySubscribed ||
		gRemoteDisplayNotifyCooldown_10ms > 0 || !gRemoteDirtyPages)
		return false;

	*pDirtyMask = gRemoteDirtyPages;
	for (uint8_t Page = 0; Page < REMOTE_DISPLAY_PAGE_COUNT; Page++)
		PageRevision[Page] = gRemotePageRevision[Page];

	gRemoteDirtyPages = 0;
	gRemoteDisplayNotifyCooldown_10ms = REMOTE_DISPLAY_NOTIFY_INTERVAL_10MS;
	return true;
}

REMOTE_Status_t REMOTE_SetPtt(const bool Pressed)
{
	if (!Pressed)
	{
		REMOTE_ReleasePtt();
		REMOTE_RecordPttChange(false);
		return REMOTE_STATUS_OK;
	}
	if (!gRemotePttPressed && gCurrentFunction == FUNCTION_TRANSMIT)
		return REMOTE_STATUS_BAD_STATE;
	if (!gRemotePttPressed)
	{
		gRemotePttPressed = true;
		APP_ProcessKey(KEY_PTT, true, false);
	}
	gRemotePttLease_10ms = REMOTE_KEY_LEASE_10MS;
	REMOTE_RecordPttChange(false);
	return REMOTE_STATUS_OK;
}

REMOTE_Status_t REMOTE_KeyEvent(const KEY_Code_t Key, const REMOTE_KeyAction_t Action)
{
	if (!gRemoteInitialized)
		return REMOTE_STATUS_BAD_STATE;
	if (Key >= KEY_INVALID || Key == KEY_PTT)
		return REMOTE_STATUS_BAD_KEY;
	if (Action > REMOTE_KEY_ACTION_RENEW)
		return REMOTE_STATUS_BAD_STATE;

	switch (Action)
	{
		case REMOTE_KEY_ACTION_DOWN:
			if (gRemoteKey.Key == Key)
			{
				gRemoteKey.Lease_10ms = REMOTE_KEY_LEASE_10MS;
				return REMOTE_STATUS_OK;
			}

			REMOTE_ReleaseKey();
			gRemoteKey.Key            = Key;
			gRemoteKey.Lease_10ms     = REMOTE_KEY_LEASE_10MS;
			gRemoteKey.PressTime_10ms = 0;
			gRemoteKey.Held           = false;
			APP_ProcessKey(Key, true, false);
			return REMOTE_STATUS_OK;

		case REMOTE_KEY_ACTION_UP:
			if (gRemoteKey.Key != Key)
				return REMOTE_STATUS_BAD_STATE;
			REMOTE_ReleaseKey();
			return REMOTE_STATUS_OK;

		case REMOTE_KEY_ACTION_RENEW:
			if (gRemoteKey.Key != Key)
				return REMOTE_STATUS_BAD_STATE;
			gRemoteKey.Lease_10ms = REMOTE_KEY_LEASE_10MS;
			return REMOTE_STATUS_OK;
	}

	return REMOTE_STATUS_BAD_STATE;
}

void REMOTE_ReleaseAll(void)
{
	REMOTE_ReleaseKey();
	REMOTE_ReleasePtt();
}

void REMOTE_ReleaseKeys(void)
{
	REMOTE_ReleaseKey();
}

void REMOTE_TimeSlice10ms(void)
{
	if (gRemoteDisplayNotifyCooldown_10ms > 0)
		gRemoteDisplayNotifyCooldown_10ms--;

	if (gRemoteKey.Key != KEY_INVALID)
	{
		if (gRemoteKey.Lease_10ms > 0 && --gRemoteKey.Lease_10ms == 0)
		{
			REMOTE_ReleaseKey();
		}
		else
		{
			gRemoteKey.PressTime_10ms++;
			if (gRemoteKey.PressTime_10ms == key_repeat_delay_10ms)
			{
				gRemoteKey.Held = true;
				APP_ProcessKey(gRemoteKey.Key, true, true);
			}
			else if (gRemoteKey.Held &&
				(gRemoteKey.Key == KEY_UP || gRemoteKey.Key == KEY_DOWN) &&
				((gRemoteKey.PressTime_10ms - key_repeat_delay_10ms) % key_repeat_10ms) == 0)
			{
				APP_ProcessKey(gRemoteKey.Key, true, true);
			}
		}
	}

	if (gRemotePttPressed && gRemotePttLease_10ms > 0 && --gRemotePttLease_10ms == 0)
		REMOTE_ReleasePtt();

	REMOTE_RecordPttChange(true);
}

bool REMOTE_IsPttPressed(void)
{
	return gRemotePttPressed;
}

bool REMOTE_IsPttActive(void)
{
	return gRemotePttPressed || gCurrentFunction == FUNCTION_TRANSMIT;
}

bool REMOTE_TakePttChange(bool *pActive)
{
	if (!gRemoteInitialized || !gRemotePttDirty)
		return false;
	gRemotePttDirty = false;
	*pActive = REMOTE_IsPttActive();
	return true;
}

bool REMOTE_IsKeyPressed(void)
{
	return gRemoteKey.Key != KEY_INVALID;
}
