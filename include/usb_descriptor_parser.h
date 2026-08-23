#ifndef DUALSENSE_USB_DESCRIPTOR_PARSER_H
#define DUALSENSE_USB_DESCRIPTOR_PARSER_H

#include <stddef.h>
#include <stdint.h>

#define DS_USB_PARSE_OK 0
#define DS_USB_PARSE_INVALID -1
#define DS_USB_PARSE_NOT_FOUND -2

typedef struct ds_usb_layout {
    const uint8_t *input_endpoint;
    const uint8_t *output_endpoint;
    uint8_t configuration_value;
    uint8_t interface_count;
    uint8_t interface_number;
    uint8_t alternate_setting;
    uint8_t input_address;
    uint8_t input_interval;
    uint8_t output_address;
    uint8_t output_interval;
    uint16_t input_packet_size;
    uint16_t output_packet_size;
} ds_usb_layout;

uint16_t ds_usb_read_le16(const uint8_t *bytes);
int ds_usb_parse_configuration(const uint8_t *configuration,
                               size_t available_length,
                               ds_usb_layout *layout);

#endif
