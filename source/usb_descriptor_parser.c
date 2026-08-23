#include "usb_descriptor_parser.h"

#define USB_DESCRIPTOR_TYPE_CONFIG 0x02u
#define USB_DESCRIPTOR_TYPE_INTERFACE 0x04u
#define USB_DESCRIPTOR_TYPE_ENDPOINT 0x05u
#define USB_CONFIG_DESCRIPTOR_SIZE 9u
#define USB_INTERFACE_DESCRIPTOR_SIZE 9u
#define USB_ENDPOINT_DESCRIPTOR_SIZE 7u
#define USB_CLASS_HID 0x03u
#define USB_ENDPOINT_TRANSFER_TYPE_MASK 0x03u
#define USB_ENDPOINT_TRANSFER_TYPE_INTERRUPT 0x03u
#define USB_ENDPOINT_DIRECTION_IN 0x80u
#define USB_ENDPOINT_PACKET_SIZE_MASK 0x07ffu
#define DUALSENSE_INPUT_REPORT_SIZE 64u
#define DUALSENSE_OUTPUT_REPORT_SIZE 63u

uint16_t ds_usb_read_le16(const uint8_t *bytes)
{
    if (!bytes) {
        return 0u;
    }
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static void layout_clear(ds_usb_layout *layout)
{
    uint8_t *bytes = (uint8_t *)layout;
    size_t remaining = sizeof(*layout);

    while (remaining-- != 0u) {
        *bytes++ = 0u;
    }
}

int ds_usb_parse_configuration(const uint8_t *configuration,
                               size_t available_length,
                               ds_usb_layout *layout)
{
    const uint8_t *cursor;
    const uint8_t *end;
    const uint8_t *candidate_input = 0;
    const uint8_t *candidate_output = 0;
    uint16_t candidate_input_size = 0u;
    uint16_t candidate_output_size = 0u;
    uint16_t total_length;
    uint8_t candidate_interface = 0u;
    uint8_t candidate_alternate = 0u;
    uint8_t hid_active = 0u;

    if (!configuration || !layout || available_length < USB_CONFIG_DESCRIPTOR_SIZE ||
        configuration[0] < USB_CONFIG_DESCRIPTOR_SIZE ||
        configuration[1] != USB_DESCRIPTOR_TYPE_CONFIG) {
        return DS_USB_PARSE_INVALID;
    }

    total_length = ds_usb_read_le16(configuration + 2u);
    if ((size_t)total_length > available_length ||
        total_length < configuration[0]) {
        return DS_USB_PARSE_INVALID;
    }

    layout_clear(layout);
    layout->configuration_value = configuration[5];
    layout->interface_count = configuration[4];
    cursor = configuration;
    end = configuration + total_length;

    while (cursor < end) {
        size_t remaining = (size_t)(end - cursor);
        uint8_t descriptor_length;
        uint8_t descriptor_type;

        if (remaining < 2u) {
            return DS_USB_PARSE_INVALID;
        }
        descriptor_length = cursor[0];
        descriptor_type = cursor[1];
        if (descriptor_length < 2u || (size_t)descriptor_length > remaining) {
            return DS_USB_PARSE_INVALID;
        }

        if (descriptor_type == USB_DESCRIPTOR_TYPE_INTERFACE) {
            if (descriptor_length < USB_INTERFACE_DESCRIPTOR_SIZE) {
                return DS_USB_PARSE_INVALID;
            }
            if (hid_active && candidate_input) {
                break;
            }

            hid_active = cursor[5] == USB_CLASS_HID;
            candidate_input = 0;
            candidate_output = 0;
            candidate_input_size = 0u;
            candidate_output_size = 0u;
            if (hid_active) {
                candidate_interface = cursor[2];
                candidate_alternate = cursor[3];
            }
        } else if (descriptor_type == USB_DESCRIPTOR_TYPE_ENDPOINT && hid_active) {
            uint8_t direction;
            uint16_t packet_size;

            if (descriptor_length < USB_ENDPOINT_DESCRIPTOR_SIZE) {
                return DS_USB_PARSE_INVALID;
            }
            if ((cursor[3] & USB_ENDPOINT_TRANSFER_TYPE_MASK) !=
                USB_ENDPOINT_TRANSFER_TYPE_INTERRUPT) {
                cursor += descriptor_length;
                continue;
            }

            direction = cursor[2] & USB_ENDPOINT_DIRECTION_IN;
            packet_size = ds_usb_read_le16(cursor + 4u) &
                          USB_ENDPOINT_PACKET_SIZE_MASK;
            if (direction == USB_ENDPOINT_DIRECTION_IN && !candidate_input &&
                packet_size >= DUALSENSE_INPUT_REPORT_SIZE) {
                candidate_input = cursor;
                candidate_input_size = packet_size;
            } else if (direction != USB_ENDPOINT_DIRECTION_IN &&
                       !candidate_output &&
                       packet_size >= DUALSENSE_OUTPUT_REPORT_SIZE) {
                candidate_output = cursor;
                candidate_output_size = packet_size;
            }
        }
        cursor += descriptor_length;
    }

    if (!hid_active || !candidate_input) {
        return DS_USB_PARSE_NOT_FOUND;
    }

    layout->input_endpoint = candidate_input;
    layout->output_endpoint = candidate_output;
    layout->interface_number = candidate_interface;
    layout->alternate_setting = candidate_alternate;
    layout->input_address = candidate_input[2];
    layout->input_interval = candidate_input[6];
    layout->input_packet_size = candidate_input_size;
    if (candidate_output) {
        layout->output_address = candidate_output[2];
        layout->output_interval = candidate_output[6];
        layout->output_packet_size = candidate_output_size;
    }
    return DS_USB_PARSE_OK;
}
