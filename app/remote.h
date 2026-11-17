/* Copyright 2026
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef APP_REMOTE_H
#define APP_REMOTE_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/keyboard.h"

#define REMOTE_PROTOCOL_VERSION       3U
#define REMOTE_DISPLAY_PAGE_COUNT     8U
#define REMOTE_DISPLAY_PAGE_SIZE    128U

#define REMOTE_COMMAND_DISPLAY_SUBSCRIBE 0x0612U
#define REMOTE_REPLY_DISPLAY_SUBSCRIBE   0x0613U
#define REMOTE_COMMAND_DISPLAY_PAGE   0x0614U
#define REMOTE_REPLY_DISPLAY_PAGE     0x0615U
#define REMOTE_COMMAND_KEY_EVENT      0x0616U
#define REMOTE_REPLY_KEY_EVENT        0x0617U
#define REMOTE_COMMAND_RELEASE_ALL    0x0618U
#define REMOTE_REPLY_RELEASE_ALL      0x0619U
#define REMOTE_EVENT_DISPLAY_CHANGED  0x061AU

enum REMOTE_Status_t {
	REMOTE_STATUS_OK = 0,
	REMOTE_STATUS_BAD_VERSION,
	REMOTE_STATUS_BAD_LENGTH,
	REMOTE_STATUS_BAD_PAGE,
	REMOTE_STATUS_BAD_KEY,
	REMOTE_STATUS_BAD_STATE,
	REMOTE_STATUS_POWER_OFF,
	REMOTE_STATUS_UNSUPPORTED
};
typedef enum REMOTE_Status_t REMOTE_Status_t;

enum REMOTE_KeyAction_t {
	REMOTE_KEY_ACTION_UP = 0,
	REMOTE_KEY_ACTION_DOWN,
	REMOTE_KEY_ACTION_RENEW
};
typedef enum REMOTE_KeyAction_t REMOTE_KeyAction_t;

enum REMOTE_Capability_t {
	REMOTE_CAPABILITY_DISPLAY_PUSH = 1U << 0,
	REMOTE_CAPABILITY_KEYPAD       = 1U << 1,
	REMOTE_CAPABILITY_PTT_LEASE    = 1U << 2,
	REMOTE_CAPABILITY_CAT          = 1U << 3
};

#define REMOTE_CAPABILITIES (REMOTE_CAPABILITY_DISPLAY_PUSH | REMOTE_CAPABILITY_KEYPAD | \
	REMOTE_CAPABILITY_PTT_LEASE | REMOTE_CAPABILITY_CAT)

void            REMOTE_Init(void);
REMOTE_Status_t REMOTE_Hello(uint8_t ProtocolVersion);
bool            REMOTE_IsInitialized(void);
REMOTE_Status_t REMOTE_SetDisplaySubscribed(bool Enabled, uint8_t *pDirtyMask,
	uint16_t PageRevision[REMOTE_DISPLAY_PAGE_COUNT]);
REMOTE_Status_t REMOTE_GetDisplayPage(uint8_t Page, uint8_t Data[REMOTE_DISPLAY_PAGE_SIZE],
	uint16_t *pRevision, uint16_t *pCrc);
void            REMOTE_MarkDisplayPage(uint8_t Page);
void            REMOTE_ForceDisplayRefresh(void);
bool            REMOTE_TakeDisplayChange(uint8_t *pDirtyMask,
	uint16_t PageRevision[REMOTE_DISPLAY_PAGE_COUNT]);
REMOTE_Status_t REMOTE_KeyEvent(KEY_Code_t Key, REMOTE_KeyAction_t Action);
REMOTE_Status_t REMOTE_SetPtt(bool Pressed);
void            REMOTE_ReleaseAll(void);
void            REMOTE_ReleaseKeys(void);
void            REMOTE_TimeSlice10ms(void);
bool            REMOTE_IsPttPressed(void);
bool            REMOTE_IsPttActive(void);
bool            REMOTE_TakePttChange(bool *pActive);
bool            REMOTE_IsKeyPressed(void);

#endif
