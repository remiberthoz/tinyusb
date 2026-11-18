/*
 * SPDX-FileCopyrightText: Copyright (c) 2019 Ha Thach (tinyusb.org)
 * SPDX-License-Identifier: MIT
 *
 * This file is part of the TinyUSB stack.
 */

#ifndef TUSB_CCID_HOST_H_
#define TUSB_CCID_HOST_H_

#include "ccid.h"

#ifdef __cplusplus
extern "C" {
#endif


#ifndef TUH_CCID_COMMAND_BUFFER_SIZE
  // Buffer size for a single command/response to/from CCID device.
  // See TUH_CCID_N_BUFFERS to buffer multiple commands/response.
  // Defaults to 271 (adequate for APDU protocol).
  #define TUH_CCID_BUFFER_SIZE 271
#endif

#ifndef TUH_CCID_MAX_PENDING_COMMANDS
  // Number of command/response pairs to buffer while waiting
  // for device/application to process.
  // See TUH_CCID_SINGLE_BUFFER_SIZE to adjust the individual 
  // buffer size.
  #define TUH_CCID_N_BUFFERS 1
#endif

//--------------------------------------------------------------------+
// CCID-Specific USB descriptors
//--------------------------------------------------------------------+

// RX FIFO SIZE
#ifndef CFG_TUH_CCID_RX_BUFSIZE
#define CFG_TUH_CCID_RX_BUFSIZE TUH_CCID_MAX_PENDING_COMMANDS*TUH_CCID_COMMAND_BUFFER_SIZE
#endif

// RX Endpoint size
#ifndef CFG_TUH_CCID_RX_EPSIZE
#define CFG_TUH_CCID_RX_EPSIZE TUH_CCID_COMMAND_BUFFER_SIZE
#endif

// TX FIFO SIZE
#ifndef CFG_TUH_CCID_TX_BUFSIZE
#define CFG_TUH_CCID_TX_BUFSIZE TUH_CCID_MAX_PENDING_COMMANDS*TUH_CCID_COMMAND_BUFFER_SIZE
#endif

// TX Endpoint size
#ifndef CFG_TUH_CCID_TX_EPSIZE
#define CFG_TUH_CCID_TX_EPSIZE TUH_CCID_COMMAND_BUFFER_SIZE
#endif

//--------------------------------------------------------------------+
// CCID APPLICATION API
//--------------------------------------------------------------------+

typedef enum {
  CCID_STATE_COMMAND_ALLOWED   = 0b01,
  CCID_STATE_RESPONSE_BUFFERED = 0b10,
} ccid_state_t;

uint8_t tuh_ccid_get_num_interfaces();
ccidh_reader_descriptor_t* tuh_ccid_get_reader_descriptor(uint8_t itf_num);

void tuh_ccid_init_reader(uint8_t itf);
bool tuh_ccid_is_reader_sync(uint8_t itf);
bool tuh_ccid_is_card_sync(uint8_t itf, uint8_t card);
bool tuh_ccid_is_command_sync(uint8_t itf, uint8_t card, uint8_t command_index);

void tuh_ccid_free_buffered_command(uint8_t itf, uint8_t card, uint8_t command_index);

ccid_slot_parameters_t* tuh_ccid_get_card_parameters(uint8_t itf, uint8_t card);
ccid_slot_status_t* tuh_ccid_get_card_status(uint8_t itf, uint8_t card);

ccid_data_block_t* tuh_ccid_get_data_block(uint8_t itf, uint8_t card, uint8_t command_index);

uint8_t tuh_ccid_request_get_slot_status(uint8_t daddr, uint8_t card);
uint8_t tuh_ccid_request_icc_power_on(uint8_t idx, uint8_t card, uint8_t bPowerSelect);
uint8_t tuh_ccid_request_icc_power_off(uint8_t idx, uint8_t card);
uint8_t tuh_ccid_request_get_slot_status(uint8_t idx, uint8_t card);
uint8_t tuh_ccid_request_xfr_block(uint8_t idx, uint8_t card, uint8_t bBWI, uint16_t levelParameter, uint8_t* data, uint32_t length);
uint8_t tuh_ccid_request_get_parameters(uint8_t idx, uint8_t card);
uint8_t tuh_ccid_request_reset_parameters(uint8_t idx, uint8_t card);
uint8_t tuh_ccid_request_set_parameters_t0(uint8_t idx, uint8_t card, uint8_t bmFindexDindex, uint8_t bmTCCKST0, uint8_t bGuardTimeT0, uint8_t bWaitingIntegerT0, uint8_t bClockStop);
uint8_t tuh_ccid_request_set_parameters_t1(uint8_t idx, uint8_t card, uint8_t bmFindexDindex, uint8_t bmTCCKST1, uint8_t bGuardTimeT1, uint8_t bWaitingIntegerT1, uint8_t bClockStop, uint8_t bIFSC, uint8_t bNadValue);
uint8_t tuh_ccid_request_escape(uint8_t idx, uint8_t card, uint8_t* data, uint32_t length);
uint8_t tuh_ccid_request_icc_clock(uint8_t idx, uint8_t card, bool stop);
uint8_t tuh_ccid_request_t0_apdu_set_envelope(uint8_t idx, uint8_t card, bool envelope);
uint8_t tuh_ccid_request_t0_apdu_set_get_response(uint8_t idx, uint8_t card, bool get_response);
uint8_t tuh_ccid_request_secure(uint8_t idx, uint8_t card, uint8_t BWI, uint16_t level_parameter, uint8_t* data, uint32_t length);
uint8_t tuh_ccid_request_mechanical(uint8_t idx, uint8_t card, uint8_t function);
uint8_t tuh_ccid_request_abort(uint8_t idx, uint8_t card);
uint8_t tuh_ccid_request_set_data_rate_and_clock_frequency(uint8_t idx, uint8_t card, uint32_t clock_frequency, uint32_t data_rate);


//--------------------------------------------------------------------+
// CCID APPLICATION CALLBACKS
//--------------------------------------------------------------------+

// Invoked when received a new message.
// Implementation must call tuh_ccid_reset_response_buffer_pair()
extern void tuh_ccid_rx_cb(uint8_t idx, uint8_t slot, uint8_t command_index, uint8_t* command, uint8_t* response);

// Invoked when a device with device interface is mounted
// idx is index of CCID interface in the internal pool.
extern void tuh_ccid_mount_cb(uint8_t idx);

// Invoked when a device with device interface is unmounted
extern void tuh_ccid_umount_cb(uint8_t idx);

// Invoked when a TX is complete and therefore space becomes available in TX buffer
extern void tuh_ccid_tx_complete_cb(uint8_t idx);


//--------------------------------------------------------------------+
// Internal Class Driver API
//--------------------------------------------------------------------+
bool     ccidh_init(void);
bool     ccidh_deinit(void);
uint16_t ccidh_open(uint8_t rhport, uint8_t dev_addr, const tusb_desc_interface_t *itf_desc, uint16_t max_len);
bool     ccidh_set_config(uint8_t dev_addr, uint8_t itf_num);
bool     ccidh_xfer_cb(uint8_t dev_addr, uint8_t ep_addr, xfer_result_t event, uint32_t xferred_bytes);
void     ccidh_close(uint8_t dev_addr);


#ifdef __cplusplus
}
#endif

#endif