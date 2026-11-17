// Exercise production CAT state handling without radio hardware.
#include <assert.h>
#include <stdio.h>
#include "app/uart.c"

EEPROM_Config_t gEeprom;
FUNCTION_Type_t gCurrentFunction;
VFO_Info_t *gRxVfo;
VFO_Info_t *gTxVfo;
bool gUpdateDisplay;
uint8_t gUpdateStatus;

void RADIO_SelectVfos(void)
{
	gTxVfo = &gEeprom.VfoInfo[gEeprom.TX_VFO];
	gRxVfo = &gEeprom.VfoInfo[CAT_GetRoleVfo(CAT_ROLE_RX)];
}
void RADIO_SetupRegisters(bool Switch) { (void)Switch; }
void RADIO_ConfigureSquelchAndOutputPower(VFO_Info_t *Vfo) { (void)Vfo; }
void BK4819_SetFrequency(uint32_t Frequency) { (void)Frequency; }
uint16_t BK4819_ReadRegister(BK4819_REGISTER_t Register) { (void)Register; return 0; }
void BK4819_WriteRegister(BK4819_REGISTER_t Register, uint16_t Value) { (void)Register; (void)Value; }

static char LastReply[CAT_ROLE_ALL_MAX_LENGTH + 1U];
static unsigned ReplyCount;
bool REMOTE_IsInitialized(void) { return true; }
void UART_Send(const void *Buffer, uint32_t Size)
{
	assert(Size < sizeof(LastReply));
	memcpy(LastReply, Buffer, Size);
	LastReply[Size] = 0;
	ReplyCount++;
}

static void ExpectState(const char *Expected)
{
	char Reply[CAT_ROLE_ALL_MAX_LENGTH + 1U];
	assert(CAT_BuildRoleAll(Reply) == strlen(Expected));
	assert(strcmp(Reply, Expected) == 0);
}

static void Apply(const char *Command)
{
	CAT_RoleAll_t State;
	assert(CAT_ParseRoleAll(Command, &State) == 0U);
	CAT_ApplyRoleAll(&State);
}

int main(void)
{
	gCurrentFunction = FUNCTION_FOREGROUND;
	Apply("TRAMAIN,439450000,FM,0000,431800000,FM,0770");
	ExpectState("TRAMAIN,439450000,FM,0000,431800000,FM,0770;");
	VFO_Info_t *Vfo = &gEeprom.VfoInfo[0];
	assert(Vfo->TX_OFFSET_FREQUENCY_DIRECTION == TX_OFFSET_FREQUENCY_DIRECTION_SUB);
	assert(Vfo->TX_OFFSET_FREQUENCY == 765000U);

	CAT_RecordRoleState();

	// Physical tuning follows the stored offset and separate tones.
	Vfo->freq_config_RX.Frequency += 1250U;
	RADIO_ApplyOffset(Vfo);
	ExpectState("TRAMAIN,439462500,FM,0000,431812500,FM,0770;");
	CAT_NotifyRoleStateChanges();
	assert(strcmp(LastReply, "TRAMAIN,439462500,FM,0000,431812500,FM,0770;") == 0);
	assert(ReplyCount == 1U);
	CAT_NotifyRoleStateChanges();
	assert(ReplyCount == 1U); // No duplicate push without a new physical change.

	assert(CAT_SetRoleRoute(false));
	ExpectState("TRAMAIN,439462500,FM,0000,431812500,FM,0770;");

	assert(CAT_SetRoleCtcss(CAT_ROLE_RX, 885U));
	ExpectState("TRAMAIN,439462500,FM,0885,431812500,FM,0770;");
	assert(CAT_SetRoleCtcss(CAT_ROLE_TX, 0U));
	ExpectState("TRAMAIN,439462500,FM,0885,431812500,FM,0000;");
	assert(CAT_SetRoleFrequency(CAT_ROLE_RX, 439450006U));
	ExpectState("TRAMAIN,439450010,FM,0885,431812500,FM,0000;");
	assert(CAT_SetRoleFrequency(CAT_ROLE_TX, 439550004U));
	ExpectState("TRAMAIN,439450010,FM,0885,439550000,FM,0000;");
	assert(Vfo->TX_OFFSET_FREQUENCY_DIRECTION == TX_OFFSET_FREQUENCY_DIRECTION_ADD);

	// Reverse keeps effective roles, including their tones, during CAT writes.
	Vfo->FrequencyReverse = true;
	CAT_FixVfoPointers(Vfo);
	assert(CAT_SetRoleRoute(false));
	ExpectState("TRAMAIN,439550000,FM,0000,439450010,FM,0885;");
	assert(CAT_SetRoleFrequency(CAT_ROLE_TX, 431800000U));
	ExpectState("TRAMAIN,439550000,FM,0000,431800000,FM,0885;");
	assert(!Vfo->FrequencyReverse);
	assert(CAT_SetRoleMode(CAT_ROLE_TX, "NFM", 3U));
	ExpectState("TRAMAIN,439550000,NFM,0000,431800000,NFM,0885;");

	// Invalid batches never change state; unequal modes/bandwidth remain unsupported.
	CAT_RoleAll_t State;
	assert(CAT_ParseRoleAll("TRAMAIN,439450000,FM,0000,431800000,NFM,0770", &State) == CAT_ROLE_ERROR_UNSUPPORTED);
	assert(CAT_ParseRoleAll("TRAMAIN,439450000,FM,0000,431800000,AM,0770", &State) == CAT_ROLE_ERROR_UNSUPPORTED);
	assert(CAT_ParseRoleAll("TRAMAIN,439450000,FM,0000,000000000,FM,0770", &State) == CAT_ROLE_ERROR_FREQUENCY);
	ExpectState("TRAMAIN,439550000,NFM,0000,431800000,NFM,0885;");
	gCurrentFunction = FUNCTION_TRANSMIT;
	assert(!CAT_SetRoleFrequency(CAT_ROLE_TX, 435000000U));
	assert(!CAT_SetRoleCtcss(CAT_ROLE_RX, 670U));
	ExpectState("TRAMAIN,439550000,NFM,0000,431800000,NFM,0885;");
	gCurrentFunction = FUNCTION_FOREGROUND;

	Apply("TRAMAIN,145800000,FM,0885,145800000,FM,0000");
	assert(Vfo->TX_OFFSET_FREQUENCY_DIRECTION == TX_OFFSET_FREQUENCY_DIRECTION_OFF);
	ExpectState("TRAMAIN,145800000,FM,0885,145800000,FM,0000;");
	Apply("TRACROSS,145800000,AM,0000,435100000,NFM,0770");
	assert(CAT_SetRoleFrequency(CAT_ROLE_TX, 435200000U));
	ExpectState("TRACROSS,145800000,AM,0000,435200000,NFM,0770;");
	puts("CAT MAIN independent frequency/tone tests passed");
	return 0;
}
