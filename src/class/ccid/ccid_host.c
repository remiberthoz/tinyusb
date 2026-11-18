/*
 * SPDX-FileCopyrightText: Copyright (c) 2019 Ha Thach (tinyusb.org)
 * SPDX-FileCopyrightText: Copyright (c) 2024 Heiko Kuester
 * SPDX-License-Identifier: MIT
 *
 * This file is part of the TinyUSB stack.
 *
 * Contribution
 * - Rémi Berthoz
 */

#include "tusb_option.h"

#include <stdint.h>

#if (CFG_TUH_ENABLED && CFG_TUH_CCID)

#include "host/usbh.h"
#include "host/usbh_pvt.h"

#include "ccid_host.h"

// Level where CFG_TUSB_DEBUG must be at least for this driver is logged
  #ifndef CFG_TUH_CCID_LOG_LEVEL
    #define CFG_TUH_CCID_LOG_LEVEL 2
  #endif

  #define TU_LOG_DRV(...) TU_LOG(CFG_TUH_CCID_LOG_LEVEL, __VA_ARGS__)
  #define TU_LOG_CCID(_ccid, _format, ...)                                                \
    TU_LOG_DRV("[:%u:%u] CCIDh " _format "\r\n", _ccid->daddr, _ccid->bInterfaceNumber, ##__VA_ARGS__)


#define word(variable, index) (variable[index + 1] << 8) | variable[index]
#define dword(variable, index) (variable[index + 3] << 24) | (variable[index + 2] << 16) | (variable[index + 1] << 8) | variable[index]

#define word_unpack(variable) ((uint8_t) (variable >> 8)), ((uint8_t) variable)
#define dword_unpack(variable) ((uint8_t) (variable >> 24)), ((uint8_t) (variable >> 16)), ((uint8_t) (variable >> 8)), ((uint8_t) variable)

//--------------------------------------------------------------------+
// Standard USB descriptors
//--------------------------------------------------------------------+

typedef struct {
  uint8_t command[TUH_CCID_COMMAND_BUFFER_SIZE];
  uint8_t response[TUH_CCID_COMMAND_BUFFER_SIZE];
} ccidh_pending_command_t;

typedef struct {
  ccid_slot_status_t status;
  ccid_slot_parameters_t parameters;
  uint8_t next_bSeq;
  ccidh_pending_command_t pending_commands[TUH_CCID_MAX_PENDING_COMMANDS];
} ccidh_reader_slot_t;

typedef struct {
  uint8_t daddr;
  uint8_t bInterfaceNumber;
  uint8_t bAlternateSetting;
  uint8_t iInterface;

  uint8_t interrupt_ep_addr;
  bool is_mounted;

  struct {
    tu_edpt_stream_t tx;
    tu_edpt_stream_t rx;
    uint8_t tx_ff_buf[CFG_TUH_CCID_TX_BUFSIZE];
    uint8_t rx_ff_buf[CFG_TUH_CCID_RX_BUFSIZE];
  } stream;

  ccidh_reader_descriptor_t reader_config;
  ccidh_reader_slot_t smartcards[CFG_TUH_CCID_MAX_CARDS_PER_READER];
} ccidh_interface_t;

typedef struct {
  TUH_EPBUF_DEF(tx, CFG_TUH_CCID_TX_EPSIZE);
  TUH_EPBUF_DEF(rx, CFG_TUH_CCID_RX_EPSIZE);
  TUH_EPBUF_DEF(ctrl, 8);
} ccidh_bulk_edpt_buffers_t;

static ccidh_interface_t ccidh_interfaces[CFG_TUH_CCID];
CFG_TUH_MEM_SECTION static ccidh_bulk_edpt_buffers_t ccidh_bulk_edpt_buffers[CFG_TUH_CCID];

//--------------------------------------------------------------------+
// INTERNAL OBJECT & FUNCTION DECLARATION
//--------------------------------------------------------------------+

TU_ATTR_ALWAYS_INLINE static inline ccidh_interface_t* get_itf(uint8_t itf) {
  TU_ASSERT(itf < CFG_TUH_CCID, NULL);
  ccidh_interface_t* p_ccid = &ccidh_interfaces[itf];
  return (p_ccid->daddr != 0) ? p_ccid : NULL;
}

static inline uint8_t get_idx_by_ep_addr(uint8_t daddr, uint8_t ep_addr) {
  for(uint8_t i=0; i<CFG_TUH_CCID; i++) {
    ccidh_interface_t* p_ccid = get_itf(i);
    if ((p_ccid->daddr == daddr) &&
         (ep_addr == p_ccid->interrupt_ep_addr || ep_addr == p_ccid->stream.rx.ep_addr || ep_addr == p_ccid->stream.tx.ep_addr)) {
      return i;
    }
  }
  return TUSB_INDEX_INVALID_8;
}

static uint8_t get_free_command_buffer(uint8_t itf, uint8_t card) {
  for (uint8_t command_index = 0; command_index < TUH_CCID_MAX_PENDING_COMMANDS; command_index++) {
    uint8_t* p_command = get_itf(itf)->smartcards[card].pending_commands[command_index].command;
    uint8_t bMessageType = p_command[0];
    if (bMessageType == 0) {
      return command_index;
    }
  }
  return TUSB_INDEX_INVALID_8;
}

static uint8_t get_bSeq_response_buffer(uint8_t itf, uint8_t card, uint8_t bSeq) {
  for (uint8_t i = 0; i < TUH_CCID_MAX_PENDING_COMMANDS; i++) {
    uint8_t* p_command = get_itf(itf)->smartcards[card].pending_commands[i].command;
    uint8_t commandBSeq = p_command[6];
    if (commandBSeq == bSeq) {
      return i;
    }
  }
  return TUSB_INDEX_INVALID_8;
}

//--------------------------------------------------------------------+
// APPLICATION API
//--------------------------------------------------------------------+

// request slot parameters and status on each card attached to the reader
void tuh_ccid_init_reader(uint8_t itf) {
  ccidh_reader_descriptor_t* reader = tuh_ccid_get_reader_descriptor(itf);
  for (size_t slot = 0; slot <= reader->bMaxSlotIndex && slot <= CFG_TUH_CCID_MAX_CARDS_PER_READER; slot++) {
    tuh_ccid_request_get_slot_status(itf, slot);
    tuh_ccid_request_get_parameters(itf, slot);
  }
}

bool tuh_ccid_is_reader_sync(uint8_t itf) {
  const ccidh_interface_t * p_ccid = get_itf(itf);
  for (size_t card = 0; card <= CFG_TUH_CCID_MAX_CARDS_PER_READER; card++) {
    for (size_t command_index = 0; command_index < TUH_CCID_MAX_PENDING_COMMANDS; command_index++) {
      if (p_ccid->smartcards[card].pending_commands[command_index].command[0] != 0 && p_ccid->smartcards[card].pending_commands[command_index].response[0] == 0) {
        return false;
      }
    }
  }
  return true;
}

bool tuh_ccid_is_card_sync(uint8_t itf, uint8_t card) {
  const ccidh_interface_t * p_ccid = get_itf(itf);
  for (size_t command_index = 0; command_index < TUH_CCID_MAX_PENDING_COMMANDS; command_index++) {
    if (p_ccid->smartcards[card].pending_commands[command_index].command[0] != 0 && p_ccid->smartcards[card].pending_commands[command_index].response[0] == 0) {
      return false;
    }
  }
  return true;
}

bool tuh_ccid_is_command_sync(uint8_t itf, uint8_t card, uint8_t command_index) {
  const ccidh_interface_t * p_ccid = get_itf(itf);
  if (p_ccid->smartcards[card].pending_commands[command_index].command[0] != 0 && p_ccid->smartcards[card].pending_commands[command_index].response[0] == 0) {
    return false;
  }
  return true;
}

uint8_t tuh_ccid_itf_get_index(uint8_t daddr, uint8_t itf_num) {
  for (uint8_t i = 0; i < CFG_TUH_CCID; i++) {
    const ccidh_interface_t * p_ccid = get_itf(i);
    if (p_ccid->daddr == daddr && p_ccid->bInterfaceNumber == itf_num) {
      return i;
    }
  }
  return TUSB_INDEX_INVALID_8;
}

// Returns the number of CCID interfaces that are open
uint8_t tuh_ccid_get_num_interfaces() {
  uint8_t n = 0;
  for (char i = 0; i < CFG_TUH_CCID; i++) {
    if (ccidh_interfaces[i].daddr > 0) {
      n += 1;
    }
  }
  return n;
}

void tuh_ccid_free_buffered_command(uint8_t itf, uint8_t card, uint8_t command_index) {
  ccidh_interface_t* p_ccid = get_itf(itf);
  ccidh_reader_slot_t* p_card = &p_ccid->smartcards[card];
  tu_memclr(&p_card->pending_commands[command_index], sizeof(ccidh_pending_command_t));
}

// Returns the reader descriptor for interface at index itf
// This descriptor may be stale if the interface is detached
ccidh_reader_descriptor_t* tuh_ccid_get_reader_descriptor(uint8_t itf) {
  return &get_itf(itf)->reader_config;
}

ccid_slot_parameters_t* tuh_ccid_get_card_parameters(uint8_t itf, uint8_t card) {
  return &get_itf(itf)->smartcards[card].parameters;
}

ccid_slot_status_t* tuh_ccid_get_card_status(uint8_t itf, uint8_t card) {
  return &get_itf(itf)->smartcards[card].status;
}

ccid_data_block_t* tuh_ccid_get_data_block(uint8_t itf, uint8_t card, uint8_t command_index) {
  return (ccid_data_block_t*) &get_itf(itf)->smartcards[card].pending_commands[command_index].response;
}

uint8_t tuh_ccid_request_icc_power_on(uint8_t itf, uint8_t card, uint8_t bPowerSelect) {
  uint8_t command_index = get_free_command_buffer(itf, card);
  if (command_index == TUSB_INDEX_INVALID_8)
    return TUSB_INDEX_INVALID_8;
  ccidh_interface_t* p_ccid = get_itf(itf);
  ccidh_reader_slot_t* p_card = &p_ccid->smartcards[card];
  uint8_t* command = p_card->pending_commands[command_index].command;
  command[0] = CCID_MSG_P2R_ICC_POWER_ON;
  command[1] = 0;
  command[2] = 0;
  command[3] = 0;
  command[4] = 0;
  command[5] = card;
  command[6] = p_card->next_bSeq++;
  command[7] = bPowerSelect;
  command[8] = 0;
  command[9] = 0;
  tu_edpt_stream_write(&p_ccid->stream.tx, command, 10);
  tu_edpt_stream_write_xfer(&p_ccid->stream.tx);
  return command_index;
}

uint8_t tuh_ccid_request_icc_power_off(uint8_t itf, uint8_t card) {
  uint8_t command_index = get_free_command_buffer(itf, card);
  if (command_index == TUSB_INDEX_INVALID_8)
    return TUSB_INDEX_INVALID_8;
  ccidh_interface_t* p_ccid = get_itf(itf);
  ccidh_reader_slot_t* p_card = &p_ccid->smartcards[card];
  uint8_t* command = p_card->pending_commands[command_index].command;
  command[0] = CCID_MSG_P2R_ICC_POWER_OFF;
  command[1] = 0;
  command[2] = 0;
  command[3] = 0;
  command[4] = 0;
  command[5] = card;
  command[6] = p_card->next_bSeq++;
  command[7] = 0;
  command[8] = 0;
  command[9] = 0;
  tu_edpt_stream_write(&p_ccid->stream.tx, command, 10);
  tu_edpt_stream_write_xfer(&p_ccid->stream.tx);
  return command_index;
}

uint8_t tuh_ccid_request_get_slot_status(uint8_t itf, uint8_t card) {
  uint8_t command_index = get_free_command_buffer(itf, card);
  if (command_index == TUSB_INDEX_INVALID_8)
    return TUSB_INDEX_INVALID_8;
  ccidh_interface_t* p_ccid = get_itf(itf);
  ccidh_reader_slot_t* p_card = &p_ccid->smartcards[card];
  uint8_t* command = p_card->pending_commands[command_index].command;
  command[0] = CCID_MSG_P2R_GET_SLOT_STATUS;
  command[1] = 0;
  command[2] = 0;
  command[3] = 0;
  command[4] = 0;
  command[5] = card;
  command[6] = p_card->next_bSeq++;
  command[7] = 0;
  command[8] = 0;
  command[9] = 0;
  tu_edpt_stream_write(&p_ccid->stream.tx, command, 10);
  tu_edpt_stream_write_xfer(&p_ccid->stream.tx);
}

uint8_t tuh_ccid_request_xfr_block(uint8_t itf, uint8_t card, uint8_t bBWI, uint16_t levelParameter, uint8_t* data, uint32_t length) {
  uint8_t command_index = get_free_command_buffer(itf, card);
  if (command_index == TUSB_INDEX_INVALID_8)
    return TUSB_INDEX_INVALID_8;
  ccidh_interface_t* p_ccid = get_itf(itf);
  ccidh_reader_slot_t* p_card = &p_ccid->smartcards[card];
  uint8_t* command_header = p_card->pending_commands[command_index].command;
  command_header[0] = CCID_MSG_P2R_XFR_BLOCK;
  command_header[1] = (length >> 0);
  command_header[2] = (length >> 8);
  command_header[3] = (length >> 16);
  command_header[4] = (length >> 24);
  command_header[5] = card;
  command_header[6] = p_card->next_bSeq++;
  command_header[7] = bBWI;
  command_header[8] = (levelParameter >> 0);
  command_header[9] = (levelParameter >> 8);
  memcpy(command_header+10, data, length);
  // TODO this most likely won't work when data length is larger than FIFO
  tu_edpt_stream_write(&p_ccid->stream.tx, command_header, 10+length);
  tu_edpt_stream_write_xfer(&p_ccid->stream.tx);
  return command_index;
}

uint8_t tuh_ccid_request_get_parameters(uint8_t itf, uint8_t card) {
  uint8_t command_index = get_free_command_buffer(itf, card);
  if (command_index == TUSB_INDEX_INVALID_8)
    return TUSB_INDEX_INVALID_8;
  ccidh_interface_t* p_ccid = get_itf(itf);
  ccidh_reader_slot_t* p_card = &p_ccid->smartcards[card];
  uint8_t* command = p_card->pending_commands[command_index].command;
  command[0] = CCID_MSG_P2R_GET_PARAMETERS;
  command[1] = 0;
  command[2] = 0;
  command[3] = 0;
  command[4] = 0;
  command[5] = card;
  command[6] = p_card->next_bSeq++;
  command[7] = 0;
  command[8] = 0;
  command[9] = 0;
  tu_edpt_stream_write(&p_ccid->stream.tx, command, 10);
  tu_edpt_stream_write_xfer(&p_ccid->stream.tx);
  return command_index;
}

uint8_t tuh_ccid_request_reset_parameters(uint8_t itf, uint8_t card) {
  uint8_t command_index = get_free_command_buffer(itf, card);
  if (command_index == TUSB_INDEX_INVALID_8)
    return TUSB_INDEX_INVALID_8;
  ccidh_interface_t* p_ccid = get_itf(itf);
  ccidh_reader_slot_t* p_card = &p_ccid->smartcards[card];
  uint8_t* command = p_card->pending_commands[command_index].command;
  command[0] = CCID_MSG_P2R_RESET_PARAMETERS;
  command[1] = 0;
  command[2] = 0;
  command[3] = 0;
  command[4] = 0;
  command[5] = card;
  command[6] = p_card->next_bSeq++;
  command[7] = 0;
  command[8] = 0;
  command[9] = 0;
  tu_edpt_stream_write(&p_ccid->stream.tx, command, 10);
  tu_edpt_stream_write_xfer(&p_ccid->stream.tx);
  return command_index;
}

uint8_t tuh_ccid_request_set_parameters_t0(uint8_t itf, uint8_t card, uint8_t bmFindexDindex, uint8_t bmTCCKST0, uint8_t bGuardTimeT0, uint8_t bWaitingIntegerT0, uint8_t bClockStop) {
  uint8_t command_index = get_free_command_buffer(itf, card);
  if (command_index == TUSB_INDEX_INVALID_8)
    return TUSB_INDEX_INVALID_8;
  ccidh_interface_t* p_ccid = get_itf(itf);
  ccidh_reader_slot_t* p_card = &p_ccid->smartcards[card];
  uint8_t* command_w_data = p_card->pending_commands[command_index].command;
  command_w_data[0] = CCID_MSG_P2R_SET_PARAMETERS;
  command_w_data[1] = 0;
  command_w_data[2] = 0;
  command_w_data[3] = 0;
  command_w_data[4] = 5;
  command_w_data[5] = card;
  command_w_data[6] = p_card->next_bSeq++;
  command_w_data[7] = 0;
  command_w_data[8] = 0;
  command_w_data[9] = 0;
  command_w_data[10] = bmFindexDindex;
  command_w_data[11] = bmTCCKST0;
  command_w_data[12] = bGuardTimeT0;
  command_w_data[13] = bWaitingIntegerT0;
  command_w_data[14] = bClockStop;
  tu_edpt_stream_write(&p_ccid->stream.tx, command_w_data, 15);
  tu_edpt_stream_write_xfer(&p_ccid->stream.tx);
  return command_index;
}

uint8_t tuh_ccid_request_set_parameters_t1(uint8_t itf, uint8_t card, uint8_t bmFindexDindex, uint8_t bmTCCKST1, uint8_t bGuardTimeT1, uint8_t bWaitingIntegerT1, uint8_t bClockStop, uint8_t bIFSC, uint8_t bNadValue) {
  uint8_t command_index = get_free_command_buffer(itf, card);
  if (command_index == TUSB_INDEX_INVALID_8)
    return TUSB_INDEX_INVALID_8;
  ccidh_interface_t* p_ccid = get_itf(itf);
  ccidh_reader_slot_t* p_card = &p_ccid->smartcards[card];
  uint8_t* command_w_data = p_card->pending_commands[command_index].command;
  command_w_data[0] = CCID_MSG_P2R_SET_PARAMETERS;
  command_w_data[1] = 0;
  command_w_data[2] = 0;
  command_w_data[3] = 0;
  command_w_data[4] = 7;
  command_w_data[5] = card;
  command_w_data[6] = p_card->next_bSeq++;
  command_w_data[7] = 0;
  command_w_data[8] = 0;
  command_w_data[9] = 0;
  command_w_data[10] = bmFindexDindex;
  command_w_data[11] = bmTCCKST1;
  command_w_data[12] = bGuardTimeT1;
  command_w_data[13] = bWaitingIntegerT1;
  command_w_data[14] = bClockStop;
  command_w_data[15] = bIFSC;
  command_w_data[16] = bNadValue;
  tu_edpt_stream_write(&p_ccid->stream.tx, command_w_data, 17);
  tu_edpt_stream_write_xfer(&p_ccid->stream.tx);
  return command_index;
}

uint8_t tuh_ccid_request_escape(uint8_t itf, uint8_t card, uint8_t* data, uint32_t length) {
  uint8_t command_index = get_free_command_buffer(itf, card);
  if (command_index == TUSB_INDEX_INVALID_8)
    return TUSB_INDEX_INVALID_8;
  ccidh_interface_t* p_ccid = get_itf(itf);
  ccidh_reader_slot_t* p_card = &p_ccid->smartcards[card];
  uint8_t* command_header = p_card->pending_commands[command_index].command;
  command_header[0] = CCID_MSG_P2R_ESCAPE;
  command_header[1] = (length >> 24);
  command_header[2] = (length >> 16);
  command_header[3] = (length >> 8);
  command_header[4] = (length >> 0);
  command_header[5] = card;
  command_header[6] = p_card->next_bSeq++;
  command_header[7] = 0;
  command_header[8] = 0;
  command_header[9] = 0;
  memcpy(command_header+10, data, length);
  // TODO this most likely won't work when data length is larger than FIFO
  tu_edpt_stream_write(&p_ccid->stream.tx, command_header, 10+length);
  tu_edpt_stream_write_xfer(&p_ccid->stream.tx);
  return command_index;
}

uint8_t tuh_ccid_request_icc_clock(uint8_t itf, uint8_t card, bool stop) {
  uint8_t command_index = get_free_command_buffer(itf, card);
  if (command_index == TUSB_INDEX_INVALID_8)
    return TUSB_INDEX_INVALID_8;
  ccidh_interface_t* p_ccid = get_itf(itf);
  ccidh_reader_slot_t* p_card = &p_ccid->smartcards[card];
  uint8_t* command = p_card->pending_commands[command_index].command;
  command[0] = CCID_MSG_P2R_ICC_CLOCK;
  command[1] = 0;
  command[2] = 0;
  command[3] = 0;
  command[4] = 0;
  command[5] = card;
  command[6] = p_card->next_bSeq++;
  command[7] = stop;
  command[8] = 0;
  command[9] = 0;
  tu_edpt_stream_write(&p_ccid->stream.tx, command, 10);
  tu_edpt_stream_write_xfer(&p_ccid->stream.tx);
  return command_index;
}

uint8_t tuh_ccid_request_t0_apdu_set_envelope(uint8_t itf, uint8_t card, bool envelope) {
  uint8_t command_index = get_free_command_buffer(itf, card);
  if (command_index == TUSB_INDEX_INVALID_8)
    return TUSB_INDEX_INVALID_8;
  ccidh_interface_t* p_ccid = get_itf(itf);
  ccidh_reader_slot_t* p_card = &p_ccid->smartcards[card];
  uint8_t* command = p_card->pending_commands[command_index].command;
  command[0] = CCID_MSG_P2R_T0_APDU;
  command[1] = 0;
  command[2] = 0;
  command[3] = 0;
  command[4] = 0;
  command[5] = card;
  command[6] = p_card->next_bSeq++;
  command[7] = 0b00000001;
  command[8] = envelope;
  command[9] = 0;
  tu_edpt_stream_write(&p_ccid->stream.tx, command, 10);
  tu_edpt_stream_write_xfer(&p_ccid->stream.tx);
  return command_index;
}

uint8_t tuh_ccid_request_t0_apdu_set_get_response(uint8_t itf, uint8_t card, bool get_response) {
  uint8_t command_index = get_free_command_buffer(itf, card);
  if (command_index == TUSB_INDEX_INVALID_8)
    return TUSB_INDEX_INVALID_8;
  ccidh_interface_t* p_ccid = get_itf(itf);
  ccidh_reader_slot_t* p_card = &p_ccid->smartcards[card];
  uint8_t* command = p_card->pending_commands[command_index].command;
  command[0] = CCID_MSG_P2R_T0_APDU;
  command[1] = 0;
  command[2] = 0;
  command[3] = 0;
  command[4] = 0;
  command[5] = card;
  command[6] = p_card->next_bSeq++;
  command[7] = 0b00000010;
  command[8] = 0;
  command[9] = get_response;
  tu_edpt_stream_write(&p_ccid->stream.tx, command, 10);
  tu_edpt_stream_write_xfer(&p_ccid->stream.tx);
  return command_index;
}

uint8_t tuh_ccid_request_secure(uint8_t itf, uint8_t card, uint8_t BWI, uint16_t level_parameter, uint8_t* data, uint32_t length) {
  uint8_t command_index = get_free_command_buffer(itf, card);
  if (command_index == TUSB_INDEX_INVALID_8)
    return TUSB_INDEX_INVALID_8;
  ccidh_interface_t* p_ccid = get_itf(itf);
  ccidh_reader_slot_t* p_card = &p_ccid->smartcards[card];
  uint8_t* command_header = p_card->pending_commands[command_index].command;
  command_header[0] = CCID_MSG_P2R_T0_APDU;
  command_header[1] = (length >> 24);
  command_header[2] = (length >> 16);
  command_header[3] = (length >> 8);
  command_header[4] = (length >> 0);
  command_header[5] = card;
  command_header[6] = p_card->next_bSeq++;
  command_header[7] = BWI;
  command_header[8] = (level_parameter >> 8);
  command_header[9] = (level_parameter >> 0);
  memcpy(command_header+10, data, length);
  // TODO this most likely won't work when data length is larger than FIFO
  tu_edpt_stream_write(&p_ccid->stream.tx, command_header, 10+length);
  tu_edpt_stream_write_xfer(&p_ccid->stream.tx);
  return command_index;
}

uint8_t tuh_ccid_request_mechanical(uint8_t itf, uint8_t card, uint8_t function) {
  uint8_t command_index = get_free_command_buffer(itf, card);
  if (command_index == TUSB_INDEX_INVALID_8)
    return TUSB_INDEX_INVALID_8;
  ccidh_interface_t* p_ccid = get_itf(itf);
  ccidh_reader_slot_t* p_card = &p_ccid->smartcards[card];
  uint8_t* command = p_card->pending_commands[command_index].command;
  command[0] = CCID_MSG_P2R_MECHANICAL;
  command[1] = 0;
  command[2] = 0;
  command[3] = 0;
  command[4] = 0;
  command[5] = card;
  command[6] = p_card->next_bSeq++;
  command[7] = function;
  command[8] = 0;
  command[9] = 0;
  tu_edpt_stream_write(&p_ccid->stream.tx, command, 10);
  tu_edpt_stream_write_xfer(&p_ccid->stream.tx);
  return command_index;
}

uint8_t tuh_ccid_request_abort(uint8_t itf, uint8_t card) {
  uint8_t command_index = get_free_command_buffer(itf, card);
  if (command_index == TUSB_INDEX_INVALID_8)
    return TUSB_INDEX_INVALID_8;
  ccidh_interface_t* p_ccid = get_itf(itf);
  ccidh_reader_slot_t* p_card = &p_ccid->smartcards[card];
  uint8_t* command = p_card->pending_commands[command_index].command;
  command[0] = CCID_MSG_P2R_ABORT;
  command[1] = 0;
  command[2] = 0;
  command[3] = 0;
  command[4] = 0;
  command[5] = card;
  command[6] = p_card->next_bSeq++;
  command[7] = 0;
  command[8] = 0;
  command[9] = 0;
  tu_edpt_stream_write(&p_ccid->stream.tx, command, 10);
  tu_edpt_stream_write_xfer(&p_ccid->stream.tx);
  return command_index;
}

uint8_t tuh_ccid_request_set_data_rate_and_clock_frequency(uint8_t itf, uint8_t card, uint32_t clock_frequency, uint32_t data_rate) {
  uint8_t command_index = get_free_command_buffer(itf, card);
  if (command_index == TUSB_INDEX_INVALID_8)
    return TUSB_INDEX_INVALID_8;
  ccidh_interface_t* p_ccid = get_itf(itf);
  ccidh_reader_slot_t* p_card = &p_ccid->smartcards[card];
  uint8_t* command_w_data = p_card->pending_commands[command_index].command;
  command_w_data[0] = CCID_MSG_P2R_SET_DATA_RATE_AND_CLOCK_FREQUENCY;
  command_w_data[1] = 0;
  command_w_data[2] = 0;
  command_w_data[3] = 0;
  command_w_data[4] = 8;
  command_w_data[5] = card;
  command_w_data[6] = p_card->next_bSeq++;
  command_w_data[7] = 0;
  command_w_data[8] = 0;
  command_w_data[9] = 0;
  command_w_data[10] = (clock_frequency >> 24);
  command_w_data[11] = (clock_frequency >> 16);
  command_w_data[12] = (clock_frequency >> 8);
  command_w_data[13] = (clock_frequency >> 0);
  command_w_data[14] = (data_rate >> 24);
  command_w_data[15] = (data_rate >> 16);
  command_w_data[16] = (data_rate >> 8);
  command_w_data[17] = (data_rate >> 0);
  tu_edpt_stream_write(&p_ccid->stream.tx, command_w_data, 18);
  tu_edpt_stream_write_xfer(&p_ccid->stream.tx);
  return command_index;
}

//--------------------------------------------------------------------+
// APPLICATION API Callbacks
//--------------------------------------------------------------------+

TU_ATTR_WEAK void tuh_ccid_mount_cb(uint8_t itf) {
  (void) itf;
}

TU_ATTR_WEAK void tuh_ccid_umount_cb(uint8_t itf) {
  (void) itf;
}

TU_ATTR_WEAK void tuh_ccid_rx_cb(uint8_t itf, uint8_t slot, uint8_t command_index, uint8_t* command, uint8_t* response) {
  (void) itf;
  (void) slot;
}

TU_ATTR_WEAK extern void tuh_ccid_rx_with_error_cb(uint8_t itf, uint8_t slot, uint8_t command_index) {

}

TU_ATTR_WEAK extern void tuh_ccid_rx_unknown_message_cb(uint8_t itf, uint8_t slot, uint8_t command_index) {

}

TU_ATTR_WEAK void tuh_ccid_tx_complete_cb(uint8_t itf) {
  (void) itf;
}

//--------------------------------------------------------------------+
// CLASS-USBH API
//--------------------------------------------------------------------+

bool ccidh_init(void) {
  TU_LOG_DRV("sizeof(ccidh_interface_t) = %u\r\n", sizeof(ccidh_interface_t));
  tu_memclr(ccidh_interfaces, sizeof(ccidh_interfaces));
  for (size_t itf = 0; itf < CFG_TUH_CCID; itf++) {
    ccidh_interface_t *p_ccid = &ccidh_interfaces[itf];
    ccidh_bulk_edpt_buffers_t *epbuf = &ccidh_bulk_edpt_buffers[itf];
    TU_ASSERT(tu_edpt_stream_init(&p_ccid->stream.tx, true, true, false, p_ccid->stream.tx_ff_buf,
                                  CFG_TUH_CCID_TX_BUFSIZE, epbuf->tx));
    TU_ASSERT(tu_edpt_stream_init(&p_ccid->stream.rx, true, false, false, p_ccid->stream.rx_ff_buf,
                                  CFG_TUH_CCID_RX_BUFSIZE, epbuf->rx));
    for (size_t card = 0; card < CFG_TUH_CCID_MAX_CARDS_PER_READER; card++) {
      tu_memclr(&p_ccid->smartcards[card], sizeof(ccidh_reader_slot_t));
      for (size_t command_index = 0; command_index < TUH_CCID_MAX_PENDING_COMMANDS; command_index++) {
        ((ccid_response_header*) p_ccid->smartcards[card].pending_commands[command_index].command)->bSeq = TUSB_INDEX_INVALID_8;
      }
    }
  }
  return true;
}

bool ccidh_deinit(void) {
  for (size_t i = 0; i < CFG_TUH_CCID; i++) {
    ccidh_interface_t *p_ccid = get_itf(i);
    (void)tu_edpt_stream_deinit(&p_ccid->stream.tx);
    (void)tu_edpt_stream_deinit(&p_ccid->stream.rx);
  }
  return true;
}

void ccidh_close(uint8_t daddr) {
  // TODO Must also implement SUSPEND and probably application callback to perform memory cleanup
  for (uint8_t itf = 0; itf < CFG_TUH_CCID; itf++) {
    ccidh_interface_t *p_ccid = get_itf(itf);
    if (p_ccid->daddr == daddr) {
      TU_LOG_CCID(p_ccid, "close");
      tuh_ccid_umount_cb(itf); // invoke callback

      p_ccid->daddr            = 0;
      p_ccid->bInterfaceNumber = 0;
      p_ccid->is_mounted = false;
      tu_edpt_stream_close(&p_ccid->stream.tx);
      tu_edpt_stream_close(&p_ccid->stream.rx);
    }
  }
}

bool ccidh_xfer_cb(uint8_t daddr, uint8_t ep_addr, xfer_result_t event, uint32_t xferred_bytes) {
  // TODO handle stall response, retry failed transfer ...
  TU_VERIFY(event == XFER_RESULT_SUCCESS);

  uint8_t const itf = get_idx_by_ep_addr(daddr, ep_addr);
  ccidh_interface_t *p_ccid = get_itf(itf);
  TU_ASSERT(p_ccid);

  if (ep_addr == p_ccid->stream.tx.ep_addr) {
    tuh_ccid_tx_complete_cb(itf); // invoke transmit complete callback
    if (0 == tu_edpt_stream_write_xfer(&p_ccid->stream.tx)) {
      // If there is no data left, a ZLP should be sent if:
      // - xferred_bytes is multiple of EP Packet size and not zero
      (void) tu_edpt_stream_write_zlp_if_needed(&p_ccid->stream.tx, xferred_bytes);
    }

  } else if (ep_addr == p_ccid->stream.rx.ep_addr) {
    // Consume USB EDPT into FIFO
    tu_edpt_stream_read_xfer_complete(&p_ccid->stream.rx, xferred_bytes);
    // Check size of available RX
    uint8_t available = tu_edpt_stream_read_available(&p_ccid->stream.rx);
    if (available < 10) {
      goto finalize;  // Invalid: header not complete
    }
    // Consume FIFO into command header
    ccid_response_header header;
    tu_edpt_stream_read(&p_ccid->stream.rx, &header, sizeof(header));
    // Check size of available data
    if (available < header.dwLength) {
      goto finalize;  // Invalid: data block not complete
    }
    // Find corresponding command
    uint8_t command_index = get_bSeq_response_buffer(itf, header.bSlot, header.bSeq);
    // Check for errors
    if (header.bStatus & (CCID_SLOT_STATUS_COMMAND_REQUESTS_TIME_EXTENSION | CCID_SLOT_STATUS_COMMAND_FAILED | CCID_SLOT_STATUS_ICC_NOT_PRESENT | CCID_SLOT_STATUS_ICC_PRESENT_INACTIVE)) {
      // An error occurred
    }
    // Process response or call application callback
    switch (header.bMessageType) {
      case CCID_MSG_R2P_SLOT_STATUS:
        memcpy(&get_itf(itf)->smartcards[header.bSlot].status, &header, sizeof(header));
        tu_edpt_stream_read(&p_ccid->stream.rx, &get_itf(itf)->smartcards[header.bSlot].status.bClockStatus, available - sizeof(header));
        tuh_ccid_free_buffered_command(itf, header.bSlot, command_index);
        break;
      case CCID_MSG_R2P_PARAMETERS:
        memcpy(&get_itf(itf)->smartcards[header.bSlot].parameters, &header, sizeof(header));
        tu_edpt_stream_read(&p_ccid->stream.rx, get_itf(itf)->smartcards[header.bSlot].pending_commands[command_index].response + sizeof(header), available - sizeof(header));
        tuh_ccid_free_buffered_command(itf, header.bSlot, command_index);
        break;
      default:
        memcpy(get_itf(itf)->smartcards[header.bSlot].pending_commands[command_index].response, &header, sizeof(header));
        tu_edpt_stream_read(&p_ccid->stream.rx, get_itf(itf)->smartcards[header.bSlot].pending_commands[command_index].response + sizeof(header), available - sizeof(header));
        tuh_ccid_rx_cb(itf, header.bSlot, command_index, get_itf(itf)->smartcards[header.bSlot].pending_commands[command_index].command, get_itf(itf)->smartcards[header.bSlot].pending_commands[command_index].response);
        break;
    }
finalize:
    tu_edpt_stream_clear(&p_ccid->stream.rx);
    // prepare for next transfer if needed
    tu_edpt_stream_read_xfer(&p_ccid->stream.rx);
    return true;

  } else if (ep_addr == p_ccid->interrupt_ep_addr) {
    // TODO handle notification endpoint
    return true;
  } else {
    return false;
  }
}

uint16_t ccidh_open(uint8_t rhport, uint8_t daddr, const tusb_desc_interface_t *itf_desc, uint16_t max_len) {
  (void)rhport;
  const uint8_t* p_desc = (const uint8_t *) itf_desc;
  const uint8_t* desc_end = p_desc + max_len;

  TU_VERIFY(itf_desc->bInterfaceClass == TUSB_CLASS_SMART_CARD, 0);

  //------------- USB Interface descriptor -------------//
  // Check CCID interface constants
  TU_ASSERT(tu_desc_len(p_desc) <= max_len, 0);
  TU_ASSERT(itf_desc->bLength == 9, 0);
  TU_ASSERT(itf_desc->bDescriptorType == TUSB_DESC_INTERFACE, 0);
  TU_ASSERT(itf_desc->bNumEndpoints == 2 || itf_desc->bNumEndpoints == 3, 0);
  TU_ASSERT(itf_desc->bInterfaceClass == TUSB_CLASS_SMART_CARD, 0);
  TU_ASSERT(itf_desc->bInterfaceSubClass == 0, 0);
  TU_ASSERT(itf_desc->bInterfaceProtocol == 0, 0);

  // Find free CCID slot in memory
  int itf = TUSB_INDEX_INVALID_8;
  for(uint8_t i = 0; i < CFG_TUH_CCID; i++) {
    if (ccidh_interfaces[i].daddr == 0) {
      itf = i;
    }
  }
  TU_ASSERT(itf != TUSB_INDEX_INVALID_8, 0);

  // Pull CCID interface variables
  ccidh_interface_t *p_ccid = &ccidh_interfaces[itf];
  p_ccid->daddr             = daddr;
  p_ccid->bInterfaceNumber  = itf_desc->bInterfaceNumber;
  p_ccid->bAlternateSetting = itf_desc->bAlternateSetting;
  p_ccid->iInterface        = itf_desc->iInterface;
  bool has_interrupt_ep = (itf_desc->bNumEndpoints == 3);

  TU_ASSERT(p_ccid != NULL, 0);
  p_desc = tu_desc_next(p_desc);

  //------------- CCID reader descriptor -------------//
  // Check descriptor constants
  TU_ASSERT(desc_end - p_desc >= 0x36, 0);
  TU_ASSERT(0x36 == tu_desc_len(p_desc), 0);
  TU_ASSERT(0x21 == tu_desc_type(p_desc), 0);
  // Pull CCID reader variables
  ccidh_reader_descriptor_t* reader_desc = &p_ccid->reader_config;
  reader_desc->bcdCCID = word(p_desc, 2);
  reader_desc->bMaxSlotIndex = p_desc[4];
  reader_desc->bVoltageSupport = p_desc[5];
  reader_desc->dwProtocols = dword(p_desc, 6);
  reader_desc->dwDefaultClock = dword(p_desc, 10);
  reader_desc->dwMaxCCIDMessageLength = dword(p_desc, 14);
  reader_desc->bNumClockSupported = p_desc[18];
  reader_desc->dwDataRate = dword(p_desc, 19);
  reader_desc->dwMaxDataRate = dword(p_desc, 23);
  reader_desc->bNumDataRatesSupported = p_desc[27];
  reader_desc->dwMaxIFSD = p_desc[28];
  reader_desc->dwSynchProtocols = dword(p_desc, 32);
  reader_desc->dwMechanical = dword(p_desc, 36);
  reader_desc->dwFeatures = dword(p_desc, 40);
  reader_desc->dwMaxCCIDMessageLength = dword(p_desc, 44);
  reader_desc->bClassGetResponse = dword(p_desc, 48);
  reader_desc->bClassEnvelope = p_desc[49];
  reader_desc->wLcdLayout = word(p_desc, 50);
  reader_desc->bPINSupport = p_desc[52];
  reader_desc->bMaxCCIDBusySlots = p_desc[53];
  
  p_desc = tu_desc_next(p_desc);

  //-------- 1st, 2nd, (3rd) endpoint descriptors --------//
  for (int i = 0; i < 3; i++) {
    TU_ASSERT((desc_end - p_desc) >= sizeof(tusb_desc_endpoint_t), 0);
    TU_ASSERT(TUSB_DESC_ENDPOINT == tu_desc_type(p_desc), 0);
    const tusb_desc_endpoint_t* desc_ep = (const tusb_desc_endpoint_t*) p_desc;
    TU_ASSERT(tuh_edpt_open(daddr, desc_ep), 0);
    if (desc_ep->bmAttributes.xfer == TUSB_XFER_BULK &&
        tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_IN) {
      p_ccid->stream.rx.ep_addr = desc_ep->bEndpointAddress;
      tu_edpt_stream_open(&p_ccid->stream.rx, p_ccid->daddr, desc_ep, tu_edpt_packet_size(desc_ep));
      tu_edpt_stream_clear(&p_ccid->stream.rx);
    }
    else if (desc_ep->bmAttributes.xfer == TUSB_XFER_BULK &&
             tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_OUT) {
      p_ccid->stream.tx.ep_addr = desc_ep->bEndpointAddress;
      tu_edpt_stream_open(&p_ccid->stream.tx, p_ccid->daddr, desc_ep, tu_edpt_packet_size(desc_ep));
      tu_edpt_stream_clear(&p_ccid->stream.tx);
    }
    else if (desc_ep->bmAttributes.xfer == TUSB_XFER_INTERRUPT &&
             tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_IN) {
      TU_ASSERT(has_interrupt_ep, 0);
      p_ccid->interrupt_ep_addr = desc_ep->bEndpointAddress;
    }

    p_desc = tu_desc_next(p_desc);
  }

  return (void*) p_desc - (void*) itf_desc;
}

bool ccidh_set_config(uint8_t daddr, uint8_t itf_num) {
  const uint8_t     itf   = tuh_ccid_itf_get_index(daddr, itf_num);
  ccidh_interface_t *p_ccid = get_itf(itf);
  TU_ASSERT(p_ccid);
  TU_LOG_CCID(p_ccid, "set config");

  tuh_ccid_mount_cb(itf);
  tu_edpt_stream_read_xfer(&p_ccid->stream.rx);

  usbh_driver_set_config_complete(daddr, p_ccid->bInterfaceNumber);
  return true;
}


#endif
