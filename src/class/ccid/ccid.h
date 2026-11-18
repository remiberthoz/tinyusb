#ifndef TUSB_CCID_H__
#define TUSB_CCID_H__

#include "common/tusb_common.h"

#ifdef __cplusplus
extern "C" {
#endif


//-----------------------------------------------
// COMMANDS
//-----------------------------------------------

// ICC POWER ON
// response is DATA BLOCK
#define CCID_MSG_P2R_ICC_POWER_ON 0x62
#define CCID_MSG_ICC_POWER_ON_V_AUTO 0x00
#define CCID_MSG_ICC_POWER_ON_5V0 0x01
#define CCID_MSG_ICC_POWER_ON_3V0 0x02
#define CCID_MSG_ICC_POWER_ON_1V8 0x03

// ICC POWER OFF
// response is SLOT_STATUS
#define CCID_MSG_P2R_ICC_POWER_OFF 0x65

// GET SLOT STATUS
// response is SLOT_STATUS
#define CCID_MSG_P2R_GET_SLOT_STATUS 0x65

// XFR BLOCK
// response is DATA BLOCK
#define CCID_MSG_P2R_XFR_BLOCK 0x6F
#define CCID_MSG_XFER_TPDU_LEVEL 0x00
#define CCID_MSG_XFER_SHORT_APDU_LEVEL 0x00
#define CCID_MSG_XFER_EXTENDED_APDU_BEGINS_ENDS 0x00
#define CCID_MSG_XFER_EXTENDED_APDU_BEGINS 0x01
#define CCID_MSG_XFER_EXTENDED_APDU_ENDS 0x02
#define CCID_MSG_XFER_EXTENDED_APDU_CONTINUES 0x03
#define CCID_MSG_XFER_EXTENDED_APDU_EMPTY_CONTINUES 0x10

// GET PARAMETERS
// response is PARAMETERS
#define CCID_MSG_P2R_GET_PARAMETERS 0x6C

// RESET PARAMETERS
// response is PARAMETERS
#define CCID_MSG_P2R_RESET_PARAMETERS 0x6D

// SET PARAMETERS
// response is PARAMETERS
#define CCID_MSG_P2R_SET_PARAMETERS 0x61
#define CCID_MSG_SET_PARAMETERS_PROTOCOL_T0 0x00
#define CCID_MSG_SET_PARAMETERS_PROTOCOL_T1 0x01

// ESCAPE
// response is ESCAPE
#define CCID_MSG_P2R_ESCAPE 0x6B

// ICC CLOCK
// response is SLOT STATUS
#define CCID_MSG_P2R_ICC_CLOCK 0x6E
#define CCID_MSG_ICC_CLOCK_RESTART 0x00
#define CCID_MSG_ICC_CLOCK_STOP 0x01

// T0APDU
// response is SLOT STATUS
#define CCID_MSG_P2R_T0_APDU 0x6A

// SECURE
// response is SLOT STATUS
#define CCID_MSG_P2R_SECURE 0x69

// MECHANICAL
// response is SLOT STATUS
#define CCID_MSG_P2R_MECHANICAL 0x71

// ABORT
// response is SLOT STATUS
#define CCID_MSG_P2R_ABORT 0x72

// SET DATA RATE AND CLOCK FREQUENCY
// response is DATA RATE AND CLOCK FREQUENCY
#define CCID_MSG_P2R_SET_DATA_RATE_AND_CLOCK_FREQUENCY 0x73

// ABORT
// response is SLOT STATUS
#define CCID_MSG_P2R_ABORT 0x72


//-----------------------------------------------
// RESPONSES
//-----------------------------------------------

#define CCID_MSG_ERROR_CMD_ABORTED 0xFF
#define CCID_MSG_ERROR_ICC_MUTE 0xFE
#define CCID_MSG_ERROR_XFR_PARITY_ERROR 0xFD
#define CCID_MSG_ERROR_XFR_OVERRUN 0xFC
#define CCID_MSG_ERROR_HW_ERROR 0xFB
#define CCID_MSG_ERROR_BAD_ATR_TS 0xF8
#define CCID_MSG_ERROR_BAD_ATR_TCK 0xF7
#define CCID_MSG_ERROR_ICC_PROTOCOL_NOT_SUPPORTED 0xF6
#define CCID_MSG_ERROR_ICC_CLASS_NOT_SUPPORTED 0xF5
#define CCID_MSG_ERROR_PROCEDURE_BYTE_CONFLICT 0xF4
#define CCID_MSG_ERROR_DEACTIVATED_PROTOCOL 0xF3
#define CCID_MSG_ERROR_BUSY_WITH_AUTO_SEQUENCE 0xF2
#define CCID_MSG_ERROR_PIN_TIMEOUT 0xF0
#define CCID_MSG_ERROR_PIN_CANCELLED 0xEF
#define CCID_MSG_ERROR_CMD_SLOT_BUSY 0xE0
#define CCID_MSG_ERROR_USER_DEFINED 0xC0
#define CCID_MSG_ERROR_USER_DEFINED_RANGE_START 0xC0
#define CCID_MSG_ERROR_USER_DEFINED_RANGE_END 0x81
#define CCID_MSG_ERROR_RFU 0x80
#define CCID_MSG_ERROR_RFU_RANGE_START 0x80
#define CCID_MSG_ERROR_RFU_RANGE_END 0x80
#define CCID_MSG_ERROR_INCORRECT_MESSAGE_PARAMETER 0x7F
#define CCID_MSG_ERROR_INCORRECT_MESSAGE_PARAMETER_RANGE_START 0x7F
#define CCID_MSG_ERROR_INCORRECT_MESSAGE_PARAMETER_RANGE_END 0x01
#define CCID_MSG_ERROR_COMMAND_NOT_SUPPORTED 0x00

#define CCID_SLOT_STATUS_ICC_PRESENT_INACTIVE 0b00000001
#define CCID_SLOT_STATUS_ICC_NOT_PRESENT      0b00000010
#define CCID_SLOT_STATUS_COMMAND_FAILED       0b01000000
#define CCID_SLOT_STATUS_COMMAND_REQUESTS_TIME_EXTENSION 0b10000000

#define CCID_MSG_P2R_TUSB_BLANK 0
#define CCID_MSG_R2P_TUSB_BLANK 0

typedef struct {
  uint8_t bMessageType;
  uint32_t dwLength;
  uint8_t bSlot;
  uint8_t bSeq;
  uint8_t bStatus;
  uint8_t bError;
} __attribute__((packed)) ccid_response_header;

#define CCID_MSG_R2P_DATA_BLOCK 0x80
typedef struct {
  ccid_response_header header;
  uint8_t bChainParameter;
  uint8_t abData[];
} __attribute__((packed)) ccid_data_block_t;

#define CCID_MSG_R2P_SLOT_STATUS 0x81
typedef struct {
  ccid_response_header header;
  uint8_t bClockStatus;
} __attribute__((packed)) ccid_slot_status_t;

#define CCID_MSG_R2P_PARAMETERS 0x82
typedef struct {
  ccid_response_header header;
  uint8_t bProtocolNum;
  uint8_t bmFindexDindex;
  uint8_t bmTCCKS;
  uint8_t bGuardTime;
  uint8_t bmWaitingInteger;
  uint8_t bClockStop;
  uint8_t bIFSC_T1;
  uint8_t bNadValue_T1;
} __attribute__((packed)) ccid_slot_parameters_t;

#define CCID_MSG_R2P_ESCAPE 0x83
typedef struct {
  ccid_response_header header;
  uint8_t bProtocolNum;
  uint8_t abData[];
} __attribute__((packed)) ccid_escape_t;

#define CCID_MSG_R2P_DATA_RATE_AND_CLOCK_FREQUENCY 0x84
typedef struct {
  ccid_response_header header;
  uint32_t dwClockFrequency;
  uint32_t dwDataRate;
} __attribute__((packed)) ccid_data_rate_and_clock_frequency_t;


//--------------------------------------------------------------------+
// CCID-Specific USB descriptors
//--------------------------------------------------------------------+

typedef struct {
  uint16_t bcdCCID;
  uint8_t  bMaxSlotIndex;
  uint8_t  bVoltageSupport;
  uint32_t dwProtocols;
  uint32_t dwDefaultClock;
  uint32_t dwMaximumClock;
  uint8_t  bNumClockSupported;
  uint32_t dwDataRate;
  uint32_t dwMaxDataRate;
  uint8_t  bNumDataRatesSupported;
  uint32_t dwMaxIFSD;
  uint32_t dwSynchProtocols;
  uint32_t dwMechanical;
  uint32_t dwFeatures;
  uint32_t dwMaxCCIDMessageLength;
  uint8_t  bClassGetResponse;
  uint8_t  bClassEnvelope;
  uint16_t wLcdLayout;
  uint8_t  bPINSupport;
  uint8_t  bMaxCCIDBusySlots;
} ccidh_reader_descriptor_t;

//--------------------------------------------------------------------+
// CCID Interface Class
//--------------------------------------------------------------------+

typedef struct {
  ccid_slot_status_t status;
  ccid_slot_parameters_t parameters;
} ccidh_smartcard_t;

typedef enum {
    // CCID USB SPEC TABLE 5.1 field dwFeatures
    CCID_EXCHANGE_LEVEL_TPDU = 0x00010000,
    CCID_EXCHANGE_LEVEL_APDU_SHORT = 0x00020000,
    CCID_EXCHANGE_LEVEL_APDU_EXTENDENT = 0x00040000,
    CCID_EXCHANGE_LEVEL_CHARACTER = 0x00000000,
} ccid_exchange_level_t;

// # CCID USB SPEC SECTION 3.2.1
// ## PPS : Protocol and parameter selection
// ### TPDU : Transport protocol data unit

// #### TPDU PPS exchange

// TPDU PPS exchange command
typedef struct {
    uint8_t FF;
    uint8_t PPS0;
    uint8_t PPS1; // optional
    uint8_t PPS2; // optional
    uint8_t PPS3; // optional
} __attribute__((packed)) tpdu_pps_exchange_command_t;
// TPDU PPS exchange response
typedef struct {
    uint8_t FF;
    uint8_t PPS0_R;
    uint8_t PPS1_R; // optional
    uint8_t PPS2_R; // optional
    uint8_t PPS3_R; // optional
    uint8_t PCK_R;
} __attribute__((packed)) tpdu_pps_exchange_response_t;

// #### TPDU with T=0

// TPDU with T=0 FORM 1 (no data to exchange with ICC)
typedef struct {
    uint8_t CLA;
    uint8_t INS;
    uint8_t P1;
    uint8_t P2;
    // P3 = 00h added by CCID device
} __attribute__((packed)) tpdu_t0_form1_command_t;
// TPDU with T=0 FORM 1 response (no data exchanged with ICC)
typedef struct {
    uint8_t SW1;
    uint8_t SW2;
} __attribute__((packed)) tpdu_t0_form1_response_t;
// TPDU with T=0 FORM 2 (data expected from ICC)
typedef struct {
    uint8_t CLA;
    uint8_t INS;
    uint8_t P1;
    uint8_t P2;
    uint8_t Le;
} __attribute__((packed)) tpdu_t0_form2_command_t;
// TPDU with T=0 FORM 2 response (data from ICC)
typedef struct {
    uint8_t* Data;
    uint8_t SW1;
    uint8_t SW2;
} __attribute__((packed)) tpdu_t0_form2_response_t;
// TPDU with T=0 FROM 3 (data to be sent to ICC)
typedef struct {
    uint8_t CLA;
    uint8_t INS;
    uint8_t P1;
    uint8_t P2;
    uint8_t Lc;
    uint8_t Data[];
} __attribute__((packed)) tpdu_t0_form3_command_t;
// TPDU with T=0 FROM 3 response (data sent to ICC)
typedef struct {
    uint8_t SW1;
    uint8_t SW2;
} __attribute__((packed)) tpdu_t0_form3_response_t;

// ### APDU : Application protocol data unit

// ### Character


#ifdef __cplusplus
}
#endif

#endif