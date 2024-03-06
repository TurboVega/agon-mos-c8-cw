//
// Title:	AGON MOS - Bidirectional packet protocol (BDPP)
// Author:	Curtis Whitley
// Created:	20/01/2024
// Last Updated:	20/01/2024
//
// Modinfo:
// 20/01/2024:	Created initial version of protocol.

#ifndef BDP_PROTOCOL_H
#define BDP_PROTOCOL_H

#include <defines.h>

#define EZ80_COMM_PROTOCOL_VERSION		0x04	// Range is 0x04 to 0x0F, for future enhancements

#define BDPP_FLAG_ALLOWED				0x01 	// Whether bidirectional protocol is allowed (both CPUs have it)
#define BDPP_FLAG_ENABLED				0x02 	// Whether bidirectional protocol is presently enabled

#define BDPP_SMALL_DATA_SIZE			32		// Maximum payload data length for small packet
#define BDPP_MAX_DRIVER_PACKETS			16		// Maximum number of driver-owned small packets
#define BDPP_MAX_APP_PACKETS			16		// Maximum number of app-owned packets
#define BDPP_MAX_STREAMS				16		// Maximum number of command/data streams

#define BDPP_STREAM_INDEX_BITS			0xF0	// Upper nibble used for stream index
#define BDPP_PACKET_INDEX_BITS			0x0F	// Lower nibble used for packet index

#define BDPP_PACKET_START_MARKER		0x89	// Marks the beginning of a packet
#define BDPP_PACKET_ESCAPE				0x8B	// Escapes a special protocol character
#define BDPP_PACKET_START_SUBSTITUTE	0x8A	// Substitute for the packet start marker
#define BDPP_PACKET_ESCAPE_SUBSTITUTE	0x8D	// Substitute for the packet escape character
#define BDPP_PACKET_END_MARKER			0x89	// Marks the end of a packet

#define BDPP_RX_STATE_AWAIT_START		'A'		// Waiting for the packet start marker
#define BDPP_RX_STATE_AWAIT_ESC_FLAGS	'B'		// Waiting for escape or the packet flags
#define BDPP_RX_STATE_AWAIT_FLAGS		'C'		// Waiting for the packet flags
#define BDPP_RX_STATE_AWAIT_ESC_INDEX	'D'		// Waiting for escape or the packet index
#define BDPP_RX_STATE_AWAIT_INDEX		'E'		// Waiting for the packet index
#define BDPP_RX_STATE_AWAIT_ESC_SIZE	'F'		// Waiting for escape or the packet size
#define BDPP_RX_STATE_AWAIT_SIZE		'G'		// Waiting for the packet size
#define BDPP_RX_STATE_AWAIT_ESC_DATA	'H'		// Waiting for escape or a packet data byte
#define BDPP_RX_STATE_AWAIT_DATA		'I'		// Waiting for a packet data byte only
#define BDPP_RX_STATE_AWAIT_END			'J'		// Waiting for the packet end marker

#define BDPP_TX_STATE_IDLE				'K'		// Doing nothing (not transmitting)
#define BDPP_TX_STATE_SENT_START_1		'L'		// Recently sent the packet start marker
#define BDPP_TX_STATE_SENT_START_2		'M'		// Recently sent the packet start marker (again)
#define BDPP_TX_STATE_SENT_ESC_FLAGS_SS	'N'		// Recently sent escape for the flags (start substitute)
#define BDPP_TX_STATE_SENT_ESC_FLAGS_ES	'O'		// Recently sent escape for the flags (escape substitute)
#define BDPP_TX_STATE_SENT_FLAGS		'P'		// Recently sent the packet flags
#define BDPP_TX_STATE_SENT_ESC_INDEX	'Q'		// Recently sent escape for the packet index
#define BDPP_TX_STATE_SENT_INDEX		'R'		// Recently sent the packet index
#define BDPP_TX_STATE_SENT_ESC_SIZE_SS 	'S'		// Recently sent escape for the size (start substitute)
#define BDPP_TX_STATE_SENT_ESC_SIZE_ES 	'T'		// Recently sent escape for the size (escape substitute)
#define BDPP_TX_STATE_SENT_SIZE			'U'		// Recently sent the packet size
#define BDPP_TX_STATE_SENT_ESC_DATA_SS	'V'		// Recently sent escape for a data byte (start substitute)
#define BDPP_TX_STATE_SENT_ESC_DATA_ES	'W'		// Recently sent escape for a data byte (escape substitute)
#define BDPP_TX_STATE_SENT_DATA			'X'		// Recently sent a packet data byte
#define BDPP_TX_STATE_SENT_ALL_DATA		'Y'		// Recently sent the last packet data byte
#define BDPP_TX_STATE_SENT_END_1		'Z'		// Recently sent the trailing separator

#define BDPP_PKT_FLAG_PRINT				0x00	// Indicates packet contains printable data
#define BDPP_PKT_FLAG_COMMAND			0x01	// Indicates packet contains a command or request
#define BDPP_PKT_FLAG_RESPONSE			0x02	// Indicates packet contains a response
#define BDPP_PKT_FLAG_FIRST				0x04	// Indicates packet is first part of a message
#define BDPP_PKT_FLAG_MIDDLE			0x00	// Indicates packet is middle part of a message
#define BDPP_PKT_FLAG_LAST				0x08	// Indicates packet is last part of a message
#define BDPP_PKT_FLAG_ENHANCED			0x10	// Indicates packet is enhanced in some way
#define BDPP_PKT_FLAG_DONE				0x20	// Indicates packet was transmitted or received
#define BDPP_PKT_FLAG_FOR_RX			0x40	// Indicates packet is for reception, not transmission
#define BDPP_PKT_FLAG_DRIVER_OWNED		0x00	// Indicates packet is owned by the driver
#define BDPP_PKT_FLAG_APP_OWNED			0x80	// Indicates packet is owned by the application
#define BDPP_PKT_FLAG_USAGE_BITS		0x0F	// Flag bits that describe packet usage
#define BDPP_PKT_FLAG_PROCESS_BITS		0xF0	// Flag bits that affect packet processing

// Defines one packet in the transmit or receive list
//
typedef struct tag_BDPP_PACKET {
	BYTE			flags;	// Flags describing the packet
	BYTE			indexes; // Index of the packet (lower nibble) & stream (upper nibble)
	WORD			max_size; // Maximum size of the data portion
	WORD			act_size; // Actual size of the data portion
	BYTE*			data;	// Address of the data bytes
	struct tag_BDPP_PACKET* next; // Points to the next packet in the list
} BDPP_PACKET;

//----------------------------------------------------------
//*** OVERALL MANAGEMENT FROM FOREGROUND ***

// Initialize the BDPP driver.
void bdpp_fg_initialize_driver();

// Get whether BDPP is allowed (both CPUs have it)
// [BDPP API function code 0x00, signature 1]
BOOL bdpp_fg_is_allowed();

// Get whether BDPP is presently enabled
// [BDPP API function code 0x01, signature ]
BOOL bdpp_fg_is_enabled();

// Get whether BDPP is presently busy (TX or RX)
// [BDPP API function code 0x10, signature 1]
BOOL bdpp_fg_is_busy();

// Enable BDDP mode for a specific stream
// [BDPP API function code 0x02, signature 2]
BOOL bdpp_fg_enable(BYTE stream);

// Disable BDDP mode
// [BDPP API function code 0x03, signature 1]
BOOL bdpp_fg_disable();

//----------------------------------------------------------
//*** OVERALL MANAGEMENT FROM BACKGROUND ***

// Get whether BDPP is presently busy (TX or RX)
// [BDPP API function code 0x1D, signature 1]
BOOL bdpp_bg_is_busy();

//----------------------------------------------------------
//*** PACKET RECEPTION (RX) FROM FOREGROUND (MAIN THREAD) ***

// Prepare an app-owned packet for reception
// This function can fail if the packet is presently involved in a data transfer.
// The given size is a maximum, based on app memory allocation, and the
// actual size of an incoming packet may be smaller, but not larger.
// [BDPP API function code 0x05, signature 6]
BOOL bdpp_fg_prepare_rx_app_packet(BYTE index, BYTE* data, WORD size);

// Check whether an incoming app-owned packet has been received
// [BDPP API function code 0x07, signature 2]
BOOL bdpp_fg_is_rx_app_packet_done(BYTE index);

// Get the flags for a received app-owned packet.
// [BDPP API function code 0x08, signature 2]
BYTE bdpp_fg_get_rx_app_packet_flags(BYTE index);

// Get the data size for a received app-owned packet.
// [BDPP API function code 0x09, signature 8]
WORD bdpp_fg_get_rx_app_packet_size(BYTE index);

//----------------------------------------------------------
//*** PACKET TRANSMISSION (TX) FROM FOREGROUND (MAIN THREAD) ***

// Initialize an outgoing driver-owned packet, if one is available
// Returns NULL if no packet is available
volatile BDPP_PACKET* bdpp_fg_init_tx_drv_packet(BYTE flags, BYTE stream);

// Queue an app-owned packet for transmission
// The packet is expected to be full when this function is called.
// This function can fail if the packet is presently involved in a data transfer.
// [BDPP API function code 0x04, signature 7]
BOOL bdpp_fg_queue_tx_app_packet(BYTE indexes, BYTE flags, const BYTE* data, WORD size);

// Check whether an outgoing app-owned packet has been transmitted
// [BDPP API function code 0x06, signature 2]
BOOL bdpp_fg_is_tx_app_packet_done(BYTE index);

// Free the driver from using an app-owned packet
// This function can fail if the packet is presently involved in a data transfer.
// [BDPP API function code 0x0A, signature 2]
BOOL bdpp_fg_stop_using_app_packet(BYTE index);

// Start building a driver-owned, outgoing packet.
// If there is an existing packet being built, it will be flushed first.
// This returns NULL if there is no packet available.
volatile BDPP_PACKET* bdpp_fg_start_drv_tx_packet(BYTE flags, BYTE stream);

// Append a data byte to a driver-owned, outgoing packet.
// This is a blocking call, and might wait for room for data.
// [BDPP API function code 0x0B, signature 4]
void bdpp_fg_write_byte_to_drv_tx_packet(BYTE data);

// Append multiple data bytes to one or more driver-owned, outgoing packets.
// This is a blocking call, and might wait for room for data.
// [BDPP API function code 0x0C, signature 5]
void bdpp_fg_write_bytes_to_drv_tx_packet(const BYTE* data, WORD count);

// Append a single data byte to a driver-owned, outgoing packet.
// This is a potentially blocking call, and might wait for room for data.
// If necessary this function initializes and uses a new packet. It
// decides whether to use "print" data (versus "non-print" data) based on
// the value of the data. To guarantee that the packet usage flags are
// set correctly, be sure to flush the packet before switching from "print"
// to "non-print", or vice versa.
// [BDPP API function code 0x0D, signature 4]
void bdpp_fg_write_drv_tx_byte_with_usage(BYTE data);

// Append multiple data bytes to one or more driver-owned, outgoing packets.
// This is a potentially blocking call, and might wait for room for data.
// If necessary this function initializes and uses additional packets. It
// decides whether to use "print" data (versus "non-print" data) based on
// the first byte in the data. To guarantee that the packet usage flags are
// set correctly, be sure to flush the packet before switching from "print"
// to "non-print", or vice versa.
// [BDPP API function code 0x0E, signature 5]
void bdpp_fg_write_drv_tx_bytes_with_usage(const BYTE* data, WORD count);

// Flush the currently-being-built, driver-owned, outgoing packet, if any exists.
// [BDPP API function code 0x0F, signature 3]
void bdpp_fg_flush_drv_tx_packet();

//----------------------------------------------------------
//*** PACKET RECEPTION (RX) FROM BACKGROUND (ISR) ***

// Initialize an incoming driver-owned packet, if one is available
// Returns NULL if no packet is available
volatile BDPP_PACKET* bdpp_bg_init_rx_drv_packet();

// Prepare an app-owned packet for reception
// This function can fail if the packet is presently involved in a data transfer.
// The given size is a maximum, based on app memory allocation, and the
// actual size of an incoming packet may be smaller, but not larger.
// [BDPP API function code 0x12, signature 6]
BOOL bdpp_bg_prepare_rx_app_packet(BYTE index, BYTE* data, WORD size);

// Check whether an incoming app-owned packet has been received
// [BDPP API function code 0x14, signature 2]
BOOL bdpp_bg_is_rx_app_packet_done(BYTE index);

// Get the flags for a received app-owned packet.
// [BDPP API function code 0x15, signature 2]
BYTE bdpp_bg_get_rx_app_packet_flags(BYTE index);

// Get the data size for a received app-owned packet.
// [BDPP API function code 0x16, signature 8]
WORD bdpp_bg_get_rx_app_packet_size(BYTE index);

//----------------------------------------------------------
//*** PACKET TRANSMISSION (TX) FROM BACKGROUND (ISR) ***

// Initialize an outgoing driver-owned packet, if one is available
// Returns NULL if no packet is available
volatile BDPP_PACKET* bdpp_bg_init_tx_drv_packet(BYTE flags, BYTE stream);

// Queue an app-owned packet for transmission
// The packet is expected to be full when this function is called.
// This function can fail if the packet is presently involved in a data transfer.
// [BDPP API function code 0x11, signature 7]
BOOL bdpp_bg_queue_tx_app_packet(BYTE indexes, BYTE flags, const BYTE* data, WORD size);

// Check whether an outgoing app-owned packet has been transmitted
// [BDPP API function code 0x13, signature 2]
BOOL bdpp_bg_is_tx_app_packet_done(BYTE index);

// Free the driver from using an app-owned packet
// This function can fail if the packet is presently involved in a data transfer.
// [BDPP API function code 0x17, signature 2]
BOOL bdpp_bg_stop_using_app_packet(BYTE index);

// Start building a driver-owned, outgoing packet.
// If there is an existing packet being built, it will be flushed first.
// This returns NULL if there is no packet available.
volatile BDPP_PACKET* bdpp_bg_start_drv_tx_packet(BYTE flags, BYTE stream);

// Append a data byte to a driver-owned, outgoing packet.
// This is a blocking call, and might wait for room for data.
// [BDPP API function code 0x18, signature 4]
void bdpp_bg_write_byte_to_drv_tx_packet(BYTE data);

// Append multiple data bytes to one or more driver-owned, outgoing packets.
// This is a blocking call, and might wait for room for data.
// [BDPP API function code 0x19, signature 5]
void bdpp_bg_write_bytes_to_drv_tx_packet(const BYTE* data, WORD count);

// Append a single data byte to a driver-owned, outgoing packet.
// This is a potentially blocking call, and might wait for room for data.
// If necessary this function initializes and uses a new packet. It
// decides whether to use "print" data (versus "non-print" data) based on
// the value of the data. To guarantee that the packet usage flags are
// set correctly, be sure to flush the packet before switching from "print"
// to "non-print", or vice versa.
// [BDPP API function code 0x1A, signature 4]
void bdpp_bg_write_drv_tx_byte_with_usage(BYTE data);

// Append multiple data bytes to one or more driver-owned, outgoing packets.
// This is a potentially blocking call, and might wait for room for data.
// If necessary this function initializes and uses additional packets. It
// decides whether to use "print" data (versus "non-print" data) based on
// the first byte in the data. To guarantee that the packet usage flags are
// set correctly, be sure to flush the packet before switching from "print"
// to "non-print", or vice versa.
// [BDPP API function code 0x1B, signature 5]
void bdpp_bg_write_drv_tx_bytes_with_usage(const BYTE* data, WORD count);

// Flush the currently-being-built, driver-owned, outgoing packet, if any exists.
// [BDPP API function code 0x1C, signature 3]
void bdpp_bg_flush_drv_tx_packet();

#endif // BDP_PROTOCOL_H
