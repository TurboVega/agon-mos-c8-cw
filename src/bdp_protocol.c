//
// Title:	AGON MOS - Bidirectional packet protocol (BDPP)
// Author:	Curtis Whitley
// Created:	20/01/2024
// Last Updated:	20/01/2024
//
// Modinfo:
// 20/01/2024:	Created initial version of protocol.

#include "bdp_protocol.h"
#include <string.h>
#include <stdio.h>
#include "uart.h"

#define FALSE 0
#define TRUE  1

extern void uart0_handler(void);
extern void bdpp_handler(void);
extern void * set_vector(unsigned int vector, void(*handler)(void));
extern void call_vdp_protocol(BYTE data);

BYTE bdpp_driver_flags;	// Flags controlling the driver

volatile BDPP_PACKET* bdpp_free_drv_pkt_head; // Points to head of free driver packet list
volatile BDPP_PACKET* bdpp_free_drv_pkt_tail; // Points to tail of free driver packet list

BYTE bdpp_tx_state; 			// Driver transmitter state
volatile BDPP_PACKET* bdpp_tx_packet; 	// Points to the packet being transmitted
WORD bdpp_tx_byte_count; 		// Number of data bytes transmitted
volatile BDPP_PACKET* bdpp_tx_pkt_head; 	// Points to head of transmit packet list
volatile BDPP_PACKET* bdpp_tx_pkt_tail; 	// Points to tail of transmit packet list

volatile BDPP_PACKET* bdpp_fg_tx_build_packet; // Points to the packet being built
BYTE bdpp_fg_tx_next_pkt_flags; // Flags for the next transmitted packet, possibly
BYTE bdpp_fg_tx_next_stream;  	// Index of the next stream to use

volatile BDPP_PACKET* bdpp_bg_tx_build_packet; // Points to the packet being built
BYTE bdpp_bg_tx_next_pkt_flags; // Flags for the next transmitted packet, possibly
BYTE bdpp_bg_tx_next_stream;  	// Index of the next stream to use

BYTE bdpp_rx_state; 			// Driver receiver state
volatile BDPP_PACKET* bdpp_rx_packet; 	// Points to the packet being received
WORD bdpp_rx_byte_count; 		// Number of data bytes left to receive
BYTE bdpp_rx_hold_pkt_flags; 	// Flags for the received packet

volatile BDPP_PACKET* bdpp_rx_pkt_head; 	// Points to head of receive packet list
volatile BDPP_PACKET* bdpp_rx_pkt_tail; 	// Points to tail of receive packet list

// Header information for driver-owned small packets (TX and RX)
BDPP_PACKET bdpp_drv_tx_pkt_header[BDPP_MAX_DRIVER_PACKETS];
BDPP_PACKET bdpp_drv_rx_pkt_header;

// Data bytes for driver-owned small packets
BYTE bdpp_drv_tx_pkt_data[BDPP_MAX_DRIVER_PACKETS][BDPP_SMALL_DATA_SIZE];
BYTE bdpp_drv_rx_pkt_data[BDPP_SMALL_DATA_SIZE];

// Header information for app-owned packets (TX and RX)
BDPP_PACKET bdpp_app_pkt_header[BDPP_MAX_APP_PACKETS];

void bdpp_run_tx_state_machine();

//--------------------------------------------------

// Push (append) a packet to a list of packets
static volatile BDPP_PACKET* push_to_list(volatile BDPP_PACKET** head, volatile BDPP_PACKET** tail, volatile BDPP_PACKET* packet) {
	volatile BDPP_PACKET* old_head = *head;
	if (*tail) {
		(*tail)->next = packet;
	} else {
		*head = packet;
	}
	*tail = packet;
	packet->next = NULL;
	return old_head;
}

// Pull (remove) a packet from a list of packets
static volatile BDPP_PACKET* pull_from_list(volatile BDPP_PACKET** head, volatile BDPP_PACKET** tail) {
	volatile BDPP_PACKET* packet = *head;
	if (packet) {
		*head = packet->next;
		if (!packet->next) {
			*tail = NULL;
		}
		packet->next = NULL;
	}
#if DEBUG_STATE_MACHINE
	printf("pull_from_list(%p,%p) -> %p\n", head, tail, packet);
#endif
	return packet;
}

// Reset the receiver state
static void reset_receiver() {
	bdpp_rx_state = BDPP_RX_STATE_AWAIT_START;
	if (bdpp_rx_packet) {
		bdpp_rx_packet->act_size = 0;
		// Clear most flags (all except app-owned)
		bdpp_rx_packet->flags &= BDPP_PKT_FLAG_APP_OWNED;
		bdpp_rx_packet = NULL;
	}
}

// Turn on TX interrupt
//
// Intended to be called with interrupts disabled!
//
void bdpp_enable_tx_interrupt(volatile BDPP_PACKET* old_head) {
	UART0_enable_interrupt(UART_IER_TRANSMITINT|UART_IER_TRANSCOMPLETEINT);
}

//----------------------------------------------------------
//*** OVERALL MANAGEMENT ***

// Initialize the BDPP driver.
//
void bdpp_fg_initialize_driver() {
	BYTE i;
	volatile BDPP_PACKET* packet;
	UART pUART1;

	DI();
	bdpp_driver_flags = BDPP_FLAG_ALLOWED;
	bdpp_tx_state = BDPP_TX_STATE_IDLE;
	bdpp_tx_packet = NULL;
	bdpp_free_drv_pkt_head = NULL;
	bdpp_free_drv_pkt_tail = NULL;
	bdpp_tx_pkt_head = NULL;
	bdpp_tx_pkt_tail = NULL;
	bdpp_rx_pkt_head = NULL;
	bdpp_rx_pkt_tail = NULL;
	memset(bdpp_drv_tx_pkt_header, 0, sizeof(bdpp_drv_tx_pkt_header));
	memset(&bdpp_drv_rx_pkt_header, 0, sizeof(bdpp_drv_rx_pkt_header));
	memset(bdpp_app_pkt_header, 0, sizeof(bdpp_app_pkt_header));
	memset(bdpp_drv_tx_pkt_data, 0, sizeof(bdpp_drv_tx_pkt_data));
	memset(bdpp_drv_rx_pkt_data, 0, sizeof(bdpp_drv_rx_pkt_data));

	bdpp_fg_tx_build_packet = NULL;
	bdpp_fg_tx_next_pkt_flags = 0;
	bdpp_fg_tx_next_stream = 0;

	bdpp_bg_tx_build_packet = NULL;
	bdpp_bg_tx_next_pkt_flags = 0;
	bdpp_bg_tx_next_stream = 1;

	// Initialize the free driver-owned packet list
	for (i = 0; i < BDPP_MAX_DRIVER_PACKETS; i++) {
		packet = &bdpp_drv_tx_pkt_header[i];
		packet->indexes = i;
		packet->data = bdpp_drv_tx_pkt_data[i];
		push_to_list(&bdpp_free_drv_pkt_head, &bdpp_free_drv_pkt_tail,
						packet);
	}
	bdpp_drv_rx_pkt_header.data = bdpp_drv_rx_pkt_data;
	
	// Initialize the free app-owned packet list
	for (i = 0; i < BDPP_MAX_APP_PACKETS; i++) {
		packet = &bdpp_app_pkt_header[i];
		packet->indexes = i;
		packet->flags |= BDPP_PKT_FLAG_APP_OWNED;
	}

	reset_receiver();

	EI();
}

// Get whether BDPP is allowed (both CPUs have it)
//
BOOL bdpp_fg_is_allowed() {
	return ((bdpp_driver_flags & BDPP_FLAG_ALLOWED) != 0);
}

// Get whether BDPP is presently enabled
//
BOOL bdpp_fg_is_enabled() {
	return ((bdpp_driver_flags & BDPP_FLAG_ENABLED) != 0);
}

// Get whether the BDPP driver is busy (TX or RX)
//
BOOL bdpp_fg_is_busy() {
	BOOL rc;
	DI();
	rc = (bdpp_tx_state != BDPP_TX_STATE_IDLE ||
		bdpp_rx_state != BDPP_RX_STATE_AWAIT_START ||
		bdpp_tx_packet != NULL ||
		bdpp_rx_packet != NULL ||
		bdpp_tx_pkt_head != NULL ||
		bdpp_fg_tx_build_packet != NULL);
	EI();
	return rc;
}

// Get whether the BDPP driver is busy (TX or RX)
//
BOOL bdpp_bg_is_busy() {
	BOOL rc;
	rc = (bdpp_tx_state != BDPP_TX_STATE_IDLE ||
		bdpp_rx_state != BDPP_RX_STATE_AWAIT_START ||
		bdpp_tx_packet != NULL ||
		bdpp_rx_packet != NULL ||
		bdpp_tx_pkt_head != NULL ||
		bdpp_fg_tx_build_packet != NULL);
	return rc;
}

// Enable BDDP mode for a specific stream
//
BOOL bdpp_fg_enable(BYTE stream) {
	if ((bdpp_driver_flags & BDPP_FLAG_ALLOWED) && (stream < BDPP_MAX_STREAMS)) {
		bdpp_fg_flush_drv_tx_packet();
		bdpp_fg_tx_next_stream = stream;
		if (!(bdpp_driver_flags & BDPP_FLAG_ENABLED)) {
			DI();
			bdpp_driver_flags |= BDPP_FLAG_ENABLED;
			// Setup Port D bit 2 (RTS) for alt fcn (output)
			SETREG(PD_DDR, PORTPIN_TWO);
			RESETREG(PD_ALT1, PORTPIN_TWO);
			SETREG(PD_ALT2, PORTPIN_TWO);
			EI();
			set_vector(UART0_IVECT, bdpp_handler);
		}
		return TRUE;
	} else {
		return FALSE;
	}
}

// Disable BDDP mode
//
BOOL bdpp_fg_disable() {
	if (bdpp_driver_flags & BDPP_FLAG_ALLOWED) {
		if (bdpp_driver_flags & BDPP_FLAG_ENABLED) {
			while (bdpp_fg_is_busy()); // wait for BDPP to be fully idle
			bdpp_driver_flags &= ~BDPP_FLAG_ENABLED;
			set_vector(UART0_IVECT, uart0_handler);
		}
		return TRUE;
	} else {
		return FALSE;
	}
}

//----------------------------------------------------------
//*** PACKET RECEPTION (RX) FROM FOREGROUND (MAIN THREAD) ***

// Prepare an app-owned packet for reception
// This function can fail if the packet is presently involved in a data transfer.
// The given size is a maximum, based on app memory allocation, and the
// actual size of an incoming packet may be smaller, but not larger.
//
BOOL bdpp_fg_prepare_rx_app_packet(BYTE index, BYTE* data, WORD size) {
	if (bdpp_fg_is_allowed() && (index < BDPP_MAX_APP_PACKETS)) {
		volatile BDPP_PACKET* packet = &bdpp_app_pkt_header[index];
		DI();
		if (bdpp_rx_packet == packet || bdpp_tx_packet == packet) {
			EI();
			return FALSE;
		}
		packet->flags &= ~BDPP_PKT_FLAG_DONE;
		packet->flags |= BDPP_PKT_FLAG_APP_OWNED|BDPP_PKT_FLAG_FOR_RX;
		packet->max_size = size;
		packet->act_size = 0;
		packet->data = data;
		EI();
		return TRUE;
	}
	return FALSE;
}

// Check whether an incoming app-owned packet has been received
//
BOOL bdpp_fg_is_rx_app_packet_done(BYTE index) {
	BOOL rc;
	if (bdpp_fg_is_allowed() && (index < BDPP_MAX_APP_PACKETS)) {
		volatile BDPP_PACKET* packet = &bdpp_app_pkt_header[index];
		DI();
		rc = ((packet->flags & (BDPP_PKT_FLAG_FOR_RX|BDPP_PKT_FLAG_DONE)) ==
				(BDPP_PKT_FLAG_FOR_RX|BDPP_PKT_FLAG_DONE));
		EI();
		return rc;
	}
	return FALSE;
}

// Get the flags for a received app-owned packet.
BYTE bdpp_fg_get_rx_app_packet_flags(BYTE index) {
	BYTE flags = 0;
	if (bdpp_fg_is_allowed() && (index < BDPP_MAX_APP_PACKETS)) {
		volatile BDPP_PACKET* packet = &bdpp_app_pkt_header[index];
		DI();
		flags = packet->flags;
		EI();
	}
	return flags;
}

// Get the data size for a received app-owned packet.
WORD bdpp_fg_get_rx_app_packet_size(BYTE index) {
	WORD size = 0;
	if (bdpp_fg_is_allowed() && (index < BDPP_MAX_APP_PACKETS)) {
		volatile BDPP_PACKET* packet = &bdpp_app_pkt_header[index];
		DI();
		size = packet->act_size;
		EI();
	}
	return size;
}

// Free the driver from using an app-owned packet
// This function can fail if the packet is presently involved in a data transfer.
//
BOOL bdpp_fg_stop_using_app_packet(BYTE index) {
	if (bdpp_fg_is_allowed() && (index < BDPP_MAX_APP_PACKETS)) {
		volatile BDPP_PACKET* packet = &bdpp_app_pkt_header[index];
		DI();
		if (bdpp_rx_packet == packet || bdpp_tx_packet == packet) {
			EI();
			return FALSE;
		}
		packet->flags &= ~(BDPP_PKT_FLAG_DONE|BDPP_PKT_FLAG_FOR_RX);
		EI();
		return TRUE;
	}
	return FALSE;
}


//----------------------------------------------------------
//*** PACKET TRANSMISSION (TX) FROM FOREGROUND (MAIN THREAD) ***

// Initialize an outgoing driver-owned packet, if one is available
// Returns NULL if no packet is available.
//
volatile BDPP_PACKET* bdpp_fg_init_tx_drv_packet(BYTE flags, BYTE stream) {
	volatile BDPP_PACKET* packet;
	DI();
	packet = pull_from_list(&bdpp_free_drv_pkt_head, &bdpp_free_drv_pkt_tail);
	EI();
	if (packet) {
		packet->flags = flags & BDPP_PKT_FLAG_USAGE_BITS;
		packet->indexes = (packet->indexes & BDPP_PACKET_INDEX_BITS) | (stream << 4);
		packet->max_size = BDPP_SMALL_DATA_SIZE;
		packet->act_size = 0;
	}
	return packet;
}

// Queue an app-owned packet for transmission
// The packet is expected to be full when this function is called.
// This function can fail if the packet is presently involved in a data transfer.
//
BOOL bdpp_fg_queue_tx_app_packet(BYTE indexes, BYTE flags, const BYTE* data, WORD size) {
	volatile BDPP_PACKET* packet;
	BYTE index = indexes & BDPP_PACKET_INDEX_BITS;
	if (bdpp_fg_is_allowed() && (index < BDPP_MAX_APP_PACKETS)) {
		packet = &bdpp_app_pkt_header[index];
		DI();
		if (bdpp_rx_packet == packet || bdpp_tx_packet == packet) {
			EI();
			return FALSE;
		}
		flags &= ~(BDPP_PKT_FLAG_DONE|BDPP_PKT_FLAG_FOR_RX);
		flags |= BDPP_PKT_FLAG_APP_OWNED;

		packet->indexes = indexes;
		packet->max_size = size;
		packet->act_size = size;
		packet->data = (BYTE*)data;
		packet = push_to_list(&bdpp_tx_pkt_head, &bdpp_tx_pkt_tail, packet);
		bdpp_enable_tx_interrupt(packet);
		EI();
		return TRUE;
	}
	return FALSE;
}

// Check whether an outgoing app-owned packet has been transmitted
//
BOOL bdpp_fg_is_tx_app_packet_done(BYTE index) {
	BOOL rc;
	if (bdpp_fg_is_allowed() && (index < BDPP_MAX_APP_PACKETS)) {
		volatile BDPP_PACKET* packet = &bdpp_app_pkt_header[index];
		DI();
		rc = (((packet->flags & BDPP_PKT_FLAG_DONE) != 0) &&
				((packet->flags & BDPP_PKT_FLAG_FOR_RX) == 0));
		EI();
		return rc;
	}
	return FALSE;
}

// Start building a device-owned, outgoing packet.
// If there is an existing packet being built, it will be flushed first.
// This returns NULL if there is no packet available.
//
volatile BDPP_PACKET* bdpp_fg_start_drv_tx_packet(BYTE flags, BYTE stream) {
	volatile BDPP_PACKET* packet;
	bdpp_fg_flush_drv_tx_packet();
	packet = bdpp_fg_init_tx_drv_packet(flags, stream);
	return packet;
}

// Flush the currently-being-built, driver-owned, outgoing packet, if any exists.
//
int useful_int_flushes;
int useless_int_flushes;
static void bdpp_fg_internal_flush_drv_tx_packet() {
	volatile BDPP_PACKET* packet;
	if (bdpp_fg_tx_build_packet) {
		DI();
		packet = push_to_list(&bdpp_tx_pkt_head, &bdpp_tx_pkt_tail, bdpp_fg_tx_build_packet);
		bdpp_fg_tx_build_packet = NULL;
		bdpp_enable_tx_interrupt(packet);
		useful_int_flushes++;
		EI();
	} else {
		useless_int_flushes++;
	}
}

// Append a data byte to a driver-owned, outgoing packet.
// This is a blocking call, and might wait for room for data.
static void bdpp_fg_internal_write_byte_to_drv_tx_packet(BYTE data) {
	while (TRUE) {
		if (bdpp_fg_tx_build_packet) {
			BYTE* pdata = bdpp_fg_tx_build_packet->data;
			pdata[bdpp_fg_tx_build_packet->act_size++] = data;
			if (bdpp_fg_tx_build_packet->act_size >= bdpp_fg_tx_build_packet->max_size) {
				if (bdpp_fg_tx_build_packet->flags & BDPP_PKT_FLAG_LAST) {
					bdpp_fg_tx_next_pkt_flags = 0;
				} else {
					bdpp_fg_tx_next_pkt_flags = bdpp_fg_tx_build_packet->flags & ~BDPP_PKT_FLAG_FIRST;
				}
				bdpp_fg_internal_flush_drv_tx_packet();
			}
			break;
		} else {
			bdpp_fg_tx_build_packet = bdpp_fg_init_tx_drv_packet(bdpp_fg_tx_next_pkt_flags, bdpp_fg_tx_next_stream);
		}
	}
}

// Append a data byte to a driver-owned, outgoing packet.
// This is a blocking call, and might wait for room for data.
void bdpp_fg_write_byte_to_drv_tx_packet(BYTE data) {
	if (bdpp_fg_is_allowed()) {
		bdpp_fg_internal_write_byte_to_drv_tx_packet(data);
	}
}

// Append multiple data bytes to one or more driver-owned, outgoing packets.
// This is a blocking call, and might wait for room for data.
void bdpp_fg_write_bytes_to_drv_tx_packet(const BYTE* data, WORD count) {
	if (bdpp_fg_is_allowed()) {
		while (count > 0) {
			bdpp_fg_internal_write_byte_to_drv_tx_packet(*data++);
			count--;
		}
	}
}

// Append a single data byte to a driver-owned, outgoing packet.
// This is a potentially blocking call, and might wait for room for data.
// If necessary this function initializes and uses a new packet. It
// decides whether to use "print" data (versus "non-print" data) based on
// the value of the data. To guarantee that the packet usage flags are
// set correctly, be sure to flush the packet before switching from "print"
// to "non-print", or vice versa.
void bdpp_fg_write_drv_tx_byte_with_usage(BYTE data) {
	if (bdpp_fg_is_allowed()) {
		if (!bdpp_fg_tx_build_packet) {
			if (data >= 0x20 && data <= 0x7E) {
				bdpp_fg_tx_next_pkt_flags = BDPP_PKT_FLAG_FIRST|BDPP_PKT_FLAG_PRINT;
			} else {
				bdpp_fg_tx_next_pkt_flags = BDPP_PKT_FLAG_FIRST|BDPP_PKT_FLAG_COMMAND;
			}
		}
		bdpp_fg_internal_write_byte_to_drv_tx_packet(data);
	}
}

// Append multiple data bytes to one or more driver-owned, outgoing packets.
// This is a blocking call, and might wait for room for data.
// If necessary this function initializes and uses additional packets. It
// decides whether to use "print" data (versus "non-print" data) based on
// the first byte in the data. To guarantee that the packet usage flags are
// set correctly, be sure to flush the packet before switching from "print"
// to "non-print", or vice versa.
void bdpp_fg_write_drv_tx_bytes_with_usage(const BYTE* data, WORD count) {
	if (bdpp_fg_is_allowed()) {
		if (!bdpp_fg_tx_build_packet) {
			if (*data >= 0x20 && *data <= 0x7E) {
				bdpp_fg_tx_next_pkt_flags = BDPP_PKT_FLAG_FIRST|BDPP_PKT_FLAG_PRINT;
			} else {
				bdpp_fg_tx_next_pkt_flags = BDPP_PKT_FLAG_FIRST|BDPP_PKT_FLAG_COMMAND;
			}
		}
		bdpp_fg_write_bytes_to_drv_tx_packet(data, count);
	}
}

// Flush the currently-being-built, driver-owned, outgoing packet, if any exists.
//
int useful_flushes;
int useless_flushes;
WORD last_size;
void bdpp_fg_flush_drv_tx_packet() {
	if (bdpp_fg_tx_build_packet) {
		last_size = bdpp_fg_tx_build_packet->act_size;
		bdpp_fg_tx_build_packet->flags |= BDPP_PKT_FLAG_LAST;
		bdpp_fg_tx_next_stream = (bdpp_fg_tx_build_packet->indexes >> 4);
		bdpp_fg_internal_flush_drv_tx_packet();
		bdpp_fg_tx_next_pkt_flags = 0;
		useful_flushes++;
	} else {
		useless_flushes++;
	}
}

//----------------------------------------------------------
//*** PACKET RECEPTION (RX) FROM BACKGROUND (ISR) ***

// Initialize an incoming driver-owned packet, if one is available
// Returns NULL if no packet is available.
//
volatile BDPP_PACKET* bdpp_bg_init_rx_drv_packet() {
	volatile BDPP_PACKET* packet = &bdpp_drv_rx_pkt_header;
	packet->flags = 0;
	packet->max_size = BDPP_SMALL_DATA_SIZE;
	packet->act_size = 0;
	return packet;
}

// Prepare an app-owned packet for reception
// This function can fail if the packet is presently involved in a data transfer.
// The given size is a maximum, based on app memory allocation, and the
// actual size of an incoming packet may be smaller, but not larger.
//
BOOL bdpp_bg_prepare_rx_app_packet(BYTE index, BYTE* data, WORD size) {
	if (index < BDPP_MAX_APP_PACKETS) {
		volatile BDPP_PACKET* packet = &bdpp_app_pkt_header[index];
		if (bdpp_rx_packet == packet || bdpp_tx_packet == packet) {
			return FALSE;
		}
		packet->flags &= ~BDPP_PKT_FLAG_DONE;
		packet->flags |= BDPP_PKT_FLAG_APP_OWNED|BDPP_PKT_FLAG_FOR_RX;
		packet->max_size = size;
		packet->act_size = 0;
		packet->data = data;
		return TRUE;
	}
	return FALSE;
}

// Check whether an incoming app-owned packet has been received
//
BOOL bdpp_bg_is_rx_app_packet_done(BYTE index) {
	BOOL rc;
	if (index < BDPP_MAX_APP_PACKETS) {
		volatile BDPP_PACKET* packet = &bdpp_app_pkt_header[index];
		rc = ((packet->flags & (BDPP_PKT_FLAG_FOR_RX|BDPP_PKT_FLAG_DONE)) ==
				(BDPP_PKT_FLAG_FOR_RX|BDPP_PKT_FLAG_DONE));
		return rc;
	}
	return FALSE;
}

// Get the flags for a received app-owned packet.
BYTE bdpp_bg_get_rx_app_packet_flags(BYTE index) {
	BYTE flags = 0;
	if (index < BDPP_MAX_APP_PACKETS) {
		volatile BDPP_PACKET* packet = &bdpp_app_pkt_header[index];
		flags = packet->flags;
	}
	return flags;
}

// Get the data size for a received app-owned packet.
WORD bdpp_bg_get_rx_app_packet_size(BYTE index) {
	WORD size = 0;
	if (index < BDPP_MAX_APP_PACKETS) {
		volatile BDPP_PACKET* packet = &bdpp_app_pkt_header[index];
		size = packet->act_size;
	}
	return size;
}

// Free the driver from using an app-owned packet
// This function can fail if the packet is presently involved in a data transfer.
//
BOOL bdpp_bg_stop_using_app_packet(BYTE index) {
	if (index < BDPP_MAX_APP_PACKETS) {
		volatile BDPP_PACKET* packet = &bdpp_app_pkt_header[index];
		if (bdpp_rx_packet == packet || bdpp_tx_packet == packet) {
			return FALSE;
		}
		packet->flags &= ~(BDPP_PKT_FLAG_DONE|BDPP_PKT_FLAG_FOR_RX);
		return TRUE;
	}
	return FALSE;
}


//----------------------------------------------------------
//*** PACKET TRANSMISSION (TX) FROM BACKGROUND (ISR) ***

// Initialize an outgoing driver-owned packet, if one is available
// Returns NULL if no packet is available.
//
volatile BDPP_PACKET* bdpp_bg_init_tx_drv_packet(BYTE flags, BYTE stream) {
	volatile BDPP_PACKET* packet = pull_from_list(&bdpp_free_drv_pkt_head, &bdpp_free_drv_pkt_tail);
	if (packet) {
		packet->flags = flags & BDPP_PKT_FLAG_USAGE_BITS;
		packet->indexes = (packet->indexes & BDPP_PACKET_INDEX_BITS) | (stream << 4);
		packet->max_size = BDPP_SMALL_DATA_SIZE;
		packet->act_size = 0;
	}
	return packet;
}

// Queue an app-owned packet for transmission
// The packet is expected to be full when this function is called.
// This function can fail if the packet is presently involved in a data transfer.
//
BOOL bdpp_bg_queue_tx_app_packet(BYTE indexes, BYTE flags, const BYTE* data, WORD size) {
	BYTE index = indexes & BDPP_PACKET_INDEX_BITS;
	if (index < BDPP_MAX_APP_PACKETS) {
		volatile BDPP_PACKET* packet = &bdpp_app_pkt_header[index];
		if (bdpp_rx_packet == packet || bdpp_tx_packet == packet) {
			return FALSE;
		}
		flags &= ~(BDPP_PKT_FLAG_DONE|BDPP_PKT_FLAG_FOR_RX);
		flags |= BDPP_PKT_FLAG_APP_OWNED;
		packet->flags = flags;
		packet->indexes = indexes;
		packet->max_size = size;
		packet->act_size = size;
		packet->data = (BYTE*)data;
		packet->next = NULL;
		packet = push_to_list(&bdpp_tx_pkt_head, &bdpp_tx_pkt_tail, packet);
		bdpp_enable_tx_interrupt(packet);
		return TRUE;
	}
	return FALSE;
}

// Check whether an outgoing app-owned packet has been transmitted
//
BOOL bdpp_bg_is_tx_app_packet_done(BYTE index) {
	BOOL rc;
	if (index < BDPP_MAX_APP_PACKETS) {
		volatile BDPP_PACKET* packet = &bdpp_app_pkt_header[index];
		rc = (((packet->flags & BDPP_PKT_FLAG_DONE) != 0) &&
				((packet->flags & BDPP_PKT_FLAG_FOR_RX) == 0));
		return rc;
	}
	return FALSE;
}

// Start building a device-owned, outgoing packet.
// If there is an existing packet being built, it will be flushed first.
// This returns NULL if there is no packet available.
//
volatile BDPP_PACKET* bdpp_bg_start_drv_tx_packet(BYTE flags, BYTE stream) {
	volatile BDPP_PACKET* packet;
	bdpp_bg_flush_drv_tx_packet();
	packet = bdpp_bg_init_tx_drv_packet(flags, stream);
	return packet;
}

// Flush the currently-being-built, driver-owned, outgoing packet, if any exists.
//
static void bdpp_bg_internal_flush_drv_tx_packet() {
	volatile BDPP_PACKET* packet;
	if (bdpp_bg_tx_build_packet) {
			packet = push_to_list(&bdpp_tx_pkt_head, &bdpp_tx_pkt_tail, bdpp_bg_tx_build_packet);
			bdpp_bg_tx_build_packet = NULL;
			bdpp_enable_tx_interrupt(packet);
	}
}

// Append a data byte to a driver-owned, outgoing packet.
// This is a blocking call, and might wait for room for data.
static void bdpp_bg_internal_write_byte_to_drv_tx_packet(BYTE data) {
	if (!bdpp_bg_tx_build_packet) {
		bdpp_bg_tx_build_packet = bdpp_bg_init_tx_drv_packet(bdpp_bg_tx_next_pkt_flags, bdpp_bg_tx_next_stream);
	}
	if (bdpp_bg_tx_build_packet) {
		BYTE* pdata = bdpp_bg_tx_build_packet->data;
		pdata[bdpp_bg_tx_build_packet->act_size++] = data;
		if (bdpp_bg_tx_build_packet->act_size >= bdpp_bg_tx_build_packet->max_size) {
			if (bdpp_bg_tx_build_packet->flags & BDPP_PKT_FLAG_LAST) {
				bdpp_bg_tx_next_pkt_flags = 0;
			} else {
				bdpp_bg_tx_next_pkt_flags = bdpp_bg_tx_build_packet->flags & ~BDPP_PKT_FLAG_FIRST;
			}
			bdpp_bg_internal_flush_drv_tx_packet();
		}
	}
}

// Append a data byte to a driver-owned, outgoing packet.
// This is a blocking call, and might wait for room for data.
void bdpp_bg_write_byte_to_drv_tx_packet(BYTE data) {
	bdpp_bg_internal_write_byte_to_drv_tx_packet(data);
}

// Append multiple data bytes to one or more driver-owned, outgoing packets.
// This is a blocking call, and might wait for room for data.
void bdpp_bg_write_bytes_to_drv_tx_packet(const BYTE* data, WORD count) {
	while (count > 0) {
		bdpp_bg_internal_write_byte_to_drv_tx_packet(*data++);
		count--;
	}
}

// Append a single data byte to a driver-owned, outgoing packet.
// This is a potentially blocking call, and might wait for room for data.
// If necessary this function initializes and uses a new packet. It
// decides whether to use "print" data (versus "non-print" data) based on
// the value of the data. To guarantee that the packet usage flags are
// set correctly, be sure to flush the packet before switching from "print"
// to "non-print", or vice versa.
void bdpp_bg_write_drv_tx_byte_with_usage(BYTE data) {
	if (!bdpp_bg_tx_build_packet) {
		if (data >= 0x20 && data <= 0x7E) {
			bdpp_bg_tx_next_pkt_flags = BDPP_PKT_FLAG_FIRST|BDPP_PKT_FLAG_PRINT;
		} else {
			bdpp_bg_tx_next_pkt_flags = BDPP_PKT_FLAG_FIRST|BDPP_PKT_FLAG_COMMAND;
		}
	}
	bdpp_bg_internal_write_byte_to_drv_tx_packet(data);
}

// Append multiple data bytes to one or more driver-owned, outgoing packets.
// This is a blocking call, and might wait for room for data.
// If necessary this function initializes and uses additional packets. It
// decides whether to use "print" data (versus "non-print" data) based on
// the first byte in the data. To guarantee that the packet usage flags are
// set correctly, be sure to flush the packet before switching from "print"
// to "non-print", or vice versa.
void bdpp_bg_write_drv_tx_bytes_with_usage(const BYTE* data, WORD count) {
	if (!bdpp_bg_tx_build_packet) {
		if (*data >= 0x20 && *data <= 0x7E) {
			bdpp_bg_tx_next_pkt_flags = BDPP_PKT_FLAG_FIRST|BDPP_PKT_FLAG_PRINT;
		} else {
			bdpp_bg_tx_next_pkt_flags = BDPP_PKT_FLAG_FIRST|BDPP_PKT_FLAG_COMMAND;
		}
	}
	bdpp_bg_write_bytes_to_drv_tx_packet(data, count);
}

// Flush the currently-being-built, driver-owned, outgoing packet, if any exists.
//
void bdpp_bg_flush_drv_tx_packet() {
	if (bdpp_bg_tx_build_packet) {
		bdpp_bg_tx_build_packet->flags |= BDPP_PKT_FLAG_LAST;
		bdpp_bg_tx_next_stream = (bdpp_bg_tx_build_packet->indexes >> 4);
		bdpp_bg_internal_flush_drv_tx_packet();
		bdpp_bg_tx_next_pkt_flags = 0;
	}
}

//----------------------------------------------------

// Process the BDPP receiver (RX) state machine
//
void bdpp_run_rx_state_machine() {
	BYTE incoming_byte;
	BYTE packet_index;
	volatile BDPP_PACKET* packet;
	WORD i;

	while (UART0_read_lsr() & UART_LSR_DATA_READY) {
		incoming_byte = UART0_read_rbr();
		switch (bdpp_rx_state) {
			case BDPP_RX_STATE_AWAIT_START: {
				if (incoming_byte == BDPP_PACKET_START_MARKER) {
					bdpp_rx_state = BDPP_RX_STATE_AWAIT_ESC_FLAGS;
				}
			} break;

			case BDPP_RX_STATE_AWAIT_ESC_FLAGS: {
				if (incoming_byte == BDPP_PACKET_START_MARKER) {
					// stay in this state
				} else if (incoming_byte == BDPP_PACKET_ESCAPE) {
					bdpp_rx_state = BDPP_RX_STATE_AWAIT_FLAGS;
				} else {
					// We received the flags
					have_the_flags:
					bdpp_rx_hold_pkt_flags =
						(incoming_byte & (BDPP_PKT_FLAG_USAGE_BITS | BDPP_PKT_FLAG_APP_OWNED)) |
						(BDPP_PKT_FLAG_FOR_RX);
					bdpp_rx_state = BDPP_RX_STATE_AWAIT_ESC_INDEX;
				}
			} break;

			case BDPP_RX_STATE_AWAIT_FLAGS: {
				if (incoming_byte == BDPP_PACKET_START_SUBSTITUTE) {
					incoming_byte = BDPP_PACKET_START_MARKER;
					goto have_the_flags;
				} else if (incoming_byte == BDPP_PACKET_ESCAPE_SUBSTITUTE) {
					incoming_byte = BDPP_PACKET_ESCAPE;
					goto have_the_flags;
				} else {
					reset_receiver();
				}
			} break;

			case BDPP_RX_STATE_AWAIT_ESC_INDEX: {
				if (incoming_byte == BDPP_PACKET_ESCAPE) {
					bdpp_rx_state = BDPP_RX_STATE_AWAIT_INDEX;
				} else {
					// We received the index(es)
					have_the_index:
					if (bdpp_rx_hold_pkt_flags & BDPP_PKT_FLAG_APP_OWNED) {
						packet_index = incoming_byte & 0x0F;
						packet = &bdpp_app_pkt_header[packet_index];
						if (packet->flags & BDPP_PKT_FLAG_DONE) {
							reset_receiver();
						} else {
							bdpp_rx_packet = packet;
							bdpp_rx_packet->flags = bdpp_rx_hold_pkt_flags;
							bdpp_rx_state = BDPP_RX_STATE_AWAIT_ESC_SIZE;
						}
					} else {
						packet = bdpp_bg_init_rx_drv_packet();
						if (packet) {
							bdpp_rx_packet = packet;
							bdpp_rx_packet->flags = bdpp_rx_hold_pkt_flags;
							bdpp_rx_state = BDPP_RX_STATE_AWAIT_ESC_SIZE;
						} else {
							reset_receiver();
						}
					}
				}
			} break;

			case BDPP_RX_STATE_AWAIT_INDEX: {
				if (incoming_byte == BDPP_PACKET_START_SUBSTITUTE) {
					incoming_byte = BDPP_PACKET_START_MARKER;
					goto have_the_index;
				} else if (incoming_byte == BDPP_PACKET_ESCAPE_SUBSTITUTE) {
					incoming_byte = BDPP_PACKET_ESCAPE;
					goto have_the_index;
				} else {
					reset_receiver();
				}
			} break;

			case BDPP_RX_STATE_AWAIT_ESC_SIZE: {
				if (incoming_byte == BDPP_PACKET_ESCAPE) {
					bdpp_rx_state = BDPP_RX_STATE_AWAIT_SIZE;
				} else {
					// We received the size
					have_the_size:
					if (incoming_byte) {
						bdpp_rx_byte_count = (WORD)incoming_byte;
					} else {
						bdpp_rx_byte_count = 256;
					}

					if (bdpp_rx_byte_count > bdpp_rx_packet->max_size) {
						reset_receiver();
					} else {
						bdpp_rx_state = BDPP_RX_STATE_AWAIT_ESC_DATA;
					}
				}
			} break;

			case BDPP_RX_STATE_AWAIT_SIZE: {
				if (incoming_byte == BDPP_PACKET_START_SUBSTITUTE) {
					incoming_byte = BDPP_PACKET_START_MARKER;
					goto have_the_size;
				} else if (incoming_byte == BDPP_PACKET_ESCAPE_SUBSTITUTE) {
					incoming_byte = BDPP_PACKET_ESCAPE;
					goto have_the_size;
				} else {
					reset_receiver();
				}
			} break;

			case BDPP_RX_STATE_AWAIT_ESC_DATA: {
				if (incoming_byte == BDPP_PACKET_ESCAPE) {
					bdpp_rx_state = BDPP_RX_STATE_AWAIT_DATA;
				} else {
					// We received a data byte
					we_have_the_data:
					(bdpp_rx_packet->data)[bdpp_rx_packet->act_size++] = incoming_byte;
					if (--bdpp_rx_byte_count == 0) {
						// All data bytes received
						bdpp_rx_state = BDPP_RX_STATE_AWAIT_END;
					} else {
						bdpp_rx_state = BDPP_RX_STATE_AWAIT_ESC_DATA;
					}
				}
			} break;

			case BDPP_RX_STATE_AWAIT_DATA: {
				if (incoming_byte == BDPP_PACKET_START_SUBSTITUTE) {
					incoming_byte = BDPP_PACKET_START_MARKER;
					goto we_have_the_data;
				} else if (incoming_byte == BDPP_PACKET_ESCAPE_SUBSTITUTE) {
					incoming_byte = BDPP_PACKET_ESCAPE;
					goto we_have_the_data;
				} else {
					reset_receiver();
				}
			} break;

			case BDPP_RX_STATE_AWAIT_END: {
				if (incoming_byte == BDPP_PACKET_END_MARKER) {
					// Packet is complete
					bdpp_rx_packet->flags |= BDPP_PKT_FLAG_DONE;
					if (bdpp_rx_packet->flags & BDPP_PKT_FLAG_APP_OWNED) {
						bdpp_rx_packet = NULL;
					} else {
						// This is a driver-owned packet, meaning that MOS must handle it.
						for (i = 0; i < bdpp_rx_packet->act_size; i++) {
							call_vdp_protocol(bdpp_rx_packet->data[i]);
						}
					}
				}
				reset_receiver();
			} break;
		}
	}
}

// Process the BDPP transmitter (TX) state machine
//
int tx_start_cnt;
int tx_end_cnt;
WORD tx_total;
void bdpp_run_tx_state_machine() {
	BYTE outgoing_byte;
	while (UART0_read_lsr() & (UART_LSR_THREMPTY|UART_LSR_TEMT)) {
		switch (bdpp_tx_state) {
			case BDPP_TX_STATE_IDLE: {
				if (bdpp_tx_packet = pull_from_list(&bdpp_tx_pkt_head, &bdpp_tx_pkt_tail)) {
					tx_start_cnt++;
					UART0_write_thr(BDPP_PACKET_START_MARKER);
					bdpp_tx_state = BDPP_TX_STATE_SENT_START_2;
				} else {
					UART0_disable_interrupt(UART_IER_TRANSMITINT|UART_IER_TRANSCOMPLETEINT);
					return;
				}
			} break;

			//case BDPP_TX_STATE_SENT_START_1: {
			//	UART0_write_thr(BDPP_PACKET_START_MARKER);
			//	bdpp_tx_state = BDPP_TX_STATE_SENT_START_2;
			//} break;
			
			case BDPP_TX_STATE_SENT_START_2: {
				outgoing_byte = bdpp_tx_packet->flags;
				if (outgoing_byte == BDPP_PACKET_START_MARKER) {
					UART0_write_thr(BDPP_PACKET_ESCAPE);
					bdpp_tx_state = BDPP_TX_STATE_SENT_ESC_FLAGS_SS;
				} else if (outgoing_byte == BDPP_PACKET_ESCAPE) {
					UART0_write_thr(BDPP_PACKET_ESCAPE);
					bdpp_tx_state = BDPP_TX_STATE_SENT_ESC_FLAGS_ES;
				} else {
					UART0_write_thr(outgoing_byte);
					bdpp_tx_state = BDPP_TX_STATE_SENT_FLAGS;
				}
			} break;
			
			case BDPP_TX_STATE_SENT_ESC_FLAGS_SS: {
				UART0_write_thr(BDPP_PACKET_START_SUBSTITUTE);
				bdpp_tx_state = BDPP_TX_STATE_SENT_FLAGS;
			} break;

			case BDPP_TX_STATE_SENT_ESC_FLAGS_ES: {
				UART0_write_thr(BDPP_PACKET_ESCAPE_SUBSTITUTE);
				bdpp_tx_state = BDPP_TX_STATE_SENT_FLAGS;
			} break;

			case BDPP_TX_STATE_SENT_FLAGS: {
				UART0_write_thr(bdpp_tx_packet->indexes);
				bdpp_tx_state = BDPP_TX_STATE_SENT_INDEX;
			} break;

			case BDPP_TX_STATE_SENT_INDEX: {
				// This will make size 256 (0x0100) go out as 0 (0x00), on purpose.
				outgoing_byte = (BYTE)(bdpp_tx_packet->act_size);
				if (outgoing_byte == BDPP_PACKET_START_MARKER) {
					UART0_write_thr(BDPP_PACKET_ESCAPE);
					bdpp_tx_state = BDPP_TX_STATE_SENT_ESC_SIZE_SS;
				} else if (outgoing_byte == BDPP_PACKET_ESCAPE) {
					UART0_write_thr(BDPP_PACKET_ESCAPE);
					bdpp_tx_state = BDPP_TX_STATE_SENT_ESC_SIZE_ES;
				} else {
					UART0_write_thr(outgoing_byte);
					bdpp_tx_state = BDPP_TX_STATE_SENT_SIZE;
				}
			} break;

			case BDPP_TX_STATE_SENT_ESC_SIZE_SS: {
				UART0_write_thr(BDPP_PACKET_START_SUBSTITUTE);
				bdpp_tx_state = BDPP_TX_STATE_SENT_SIZE;
			} break;
			
			case BDPP_TX_STATE_SENT_ESC_SIZE_ES: {
				UART0_write_thr(BDPP_PACKET_ESCAPE_SUBSTITUTE);
				bdpp_tx_state = BDPP_TX_STATE_SENT_SIZE;
			} break;
					
			case BDPP_TX_STATE_SENT_SIZE: {
				if (bdpp_tx_packet->act_size == 0) {
					bdpp_tx_state = BDPP_TX_STATE_SENT_ALL_DATA;
				} else {
					bdpp_tx_byte_count = 0;
					bdpp_tx_state = BDPP_TX_STATE_SENT_DATA;
				}
			} break;

			case BDPP_TX_STATE_SENT_ESC_DATA_SS: {
				UART0_write_thr(BDPP_PACKET_START_SUBSTITUTE);
				check_end_of_data:
				if (++bdpp_tx_byte_count >= bdpp_tx_packet->act_size) {
					bdpp_tx_state = BDPP_TX_STATE_SENT_ALL_DATA;
				} else {
					bdpp_tx_state = BDPP_TX_STATE_SENT_DATA;
				}
			} break;

			case BDPP_TX_STATE_SENT_ESC_DATA_ES: {
				UART0_write_thr(BDPP_PACKET_ESCAPE_SUBSTITUTE);
				goto check_end_of_data;
			} break;

			case BDPP_TX_STATE_SENT_DATA: {
				outgoing_byte = (bdpp_tx_packet->data)[bdpp_tx_byte_count];
				if (outgoing_byte == BDPP_PACKET_START_MARKER) {
					UART0_write_thr(BDPP_PACKET_ESCAPE);
					bdpp_tx_state = BDPP_TX_STATE_SENT_ESC_DATA_SS;
				} else if (outgoing_byte == BDPP_PACKET_ESCAPE) {
					UART0_write_thr(BDPP_PACKET_ESCAPE);
					bdpp_tx_state = BDPP_TX_STATE_SENT_ESC_DATA_ES;
				} else {
					UART0_write_thr(outgoing_byte);
					if (++bdpp_tx_byte_count >= bdpp_tx_packet->act_size) {
						bdpp_tx_state = BDPP_TX_STATE_SENT_ALL_DATA;
					}
				}
			} break;
			
			case BDPP_TX_STATE_SENT_ALL_DATA: {
			//	UART0_write_thr(BDPP_PACKET_END_MARKER);
			//	bdpp_tx_state = BDPP_TX_STATE_SENT_END_1;
			//} break;
			
			//case BDPP_TX_STATE_SENT_END_1: {
				UART0_write_thr(BDPP_PACKET_END_MARKER); // tell the UHCI to end the packet
				tx_total += bdpp_tx_packet->act_size;
				bdpp_tx_packet->flags |= BDPP_PKT_FLAG_DONE;
				if (!(bdpp_tx_packet->flags & BDPP_PKT_FLAG_APP_OWNED)) {
					push_to_list(&bdpp_free_drv_pkt_head, &bdpp_free_drv_pkt_tail, bdpp_tx_packet);
				}
				bdpp_tx_packet = NULL;
				bdpp_tx_state = BDPP_TX_STATE_IDLE;
				tx_end_cnt++;
			} break;
		}
	}
}

// The real guts of the bidirectional packet protocol!
// This function processes the TX and RX state machines.
// It is called by the UART0 interrupt handler, so it assumes
// that interrupts are always turned off during this function.
//
void bdp_protocol() {
	BYTE iir = UART0_read_iir();
	bdpp_run_rx_state_machine();
	bdpp_run_tx_state_machine();

	/*BYTE dummy, i;
	BYTE iir = UART0_read_iir() & UART_IIR_ISCMASK;;
	if (iir == UART_IIR_CHARTIMEOUT) {
		// Read entire FIFO to clear interrupt
		for (i = 0; i < 16; i++) {
			dummy = UART0_read_rbr();
		}
	} else {
		bdpp_run_rx_state_machine();
		bdpp_run_tx_state_machine();
	}*/
}
