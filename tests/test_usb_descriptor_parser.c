#include <stdio.h>

#include "usb_descriptor_parser.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static void set_total_length(uint8_t *configuration, size_t length)
{
    configuration[2] = (uint8_t)(length & 0xffu);
    configuration[3] = (uint8_t)((length >> 8) & 0xffu);
}

static int test_composite_dualsense_layout(void)
{
    uint8_t configuration[] = {
        9, 2, 0, 0, 4, 1, 0, 0x80, 250,
        9, 4, 0, 0, 1, 1, 1, 0, 0,
        7, 5, 0x01, 0x01, 0xc0, 0x00, 1,
        9, 4, 3, 0, 2, 3, 0, 0, 0,
        9, 0x21, 0x11, 0x01, 0, 1, 0x22, 0x00, 0x01,
        7, 5, 0x84, 0x03, 0x40, 0x00, 1,
        7, 5, 0x03, 0x03, 0x40, 0x00, 1
    };
    ds_usb_layout layout;

    set_total_length(configuration, sizeof(configuration));
    CHECK(ds_usb_parse_configuration(configuration, sizeof(configuration),
                                     &layout) == DS_USB_PARSE_OK);
    CHECK(layout.configuration_value == 1u);
    CHECK(layout.interface_number == 3u);
    CHECK(layout.alternate_setting == 0u);
    CHECK(layout.input_address == 0x84u);
    CHECK(layout.input_packet_size == 64u);
    CHECK(layout.input_interval == 1u);
    CHECK(layout.output_endpoint != 0);
    CHECK(layout.output_address == 0x03u);
    CHECK(layout.output_packet_size == 64u);
    return 0;
}

static int test_rejects_short_endpoint_descriptor(void)
{
    uint8_t configuration[] = {
        9, 2, 0, 0, 1, 1, 0, 0x80, 250,
        9, 4, 3, 0, 1, 3, 0, 0, 0,
        6, 5, 0x84, 0x03, 0x40, 0x00
    };
    ds_usb_layout layout;

    set_total_length(configuration, sizeof(configuration));
    CHECK(ds_usb_parse_configuration(configuration, sizeof(configuration),
                                     &layout) == DS_USB_PARSE_INVALID);
    return 0;
}

static int test_allows_missing_or_too_small_output(void)
{
    uint8_t configuration[] = {
        9, 2, 0, 0, 1, 1, 0, 0x80, 250,
        9, 4, 3, 0, 2, 3, 0, 0, 0,
        7, 5, 0x84, 0x03, 0x40, 0x00, 1,
        7, 5, 0x03, 0x03, 0x20, 0x00, 1
    };
    ds_usb_layout layout;

    set_total_length(configuration, sizeof(configuration));
    CHECK(ds_usb_parse_configuration(configuration, sizeof(configuration),
                                     &layout) == DS_USB_PARSE_OK);
    CHECK(layout.input_endpoint != 0);
    CHECK(layout.output_endpoint == 0);
    return 0;
}

static int test_rejects_truncated_configuration(void)
{
    uint8_t configuration[] = {
        9, 2, 10, 0, 1, 1, 0, 0x80, 250
    };
    ds_usb_layout layout;

    CHECK(ds_usb_parse_configuration(configuration, sizeof(configuration),
                                     &layout) == DS_USB_PARSE_INVALID);
    return 0;
}

int main(void)
{
    if (test_composite_dualsense_layout() != 0 ||
        test_rejects_short_endpoint_descriptor() != 0 ||
        test_allows_missing_or_too_small_output() != 0 ||
        test_rejects_truncated_configuration() != 0) {
        return 1;
    }
    puts("usb descriptor parser tests passed");
    return 0;
}
