#include <usb.h>
#include <usb-cdc.h>
#include <fomu/csr.h>

static int connected = 0;
struct str_bfr
{
    uint8_t bfr_contents;
} str_bfr;

int cdc_connected(void)
{
    return connected;
}

void cdc_set_connected(int is_connected)
{
    connected = is_connected;
}

void _putchar(char character)
{
    if (character == '\n')
        _putchar('\r');

    // Wait for buffer to be empty
    while (usb_ep_2_in_respond_read() == EPF_ACK)
        ;
    usb_ep_2_in_ibuf_head_write(character);
    usb_ep_2_in_respond_write(EPF_ACK);
}

void flush_serial(void)
{
    if (!str_bfr.bfr_contents)
        return;

    usb_ep_2_in_respond_write(EPF_ACK);
    // Wait for buffer to be empty
    while (usb_ep_2_in_respond_read() == EPF_ACK)
        ;
    str_bfr.bfr_contents = 0;
}

void add_char_to_buffer(char character)
{
    while (usb_ep_2_in_respond_read() == EPF_ACK)
        ;
    usb_ep_2_in_ibuf_head_write(character);
    str_bfr.bfr_contents++;
    if (str_bfr.bfr_contents >= 64)
        flush_serial();
}

void put_string(const char *str)
{
    while (*str != '\0')
    {
        if (*str == '\n')
            add_char_to_buffer('\r');
        add_char_to_buffer(*str);
        str++;
    }
    flush_serial();
}

void put_hex(uint32_t val)
{
    int num_nibbles = sizeof(val) * 2;

    do
    {
        char v = '0' + (((val >> (num_nibbles - 1) * 4)) & 0x0f);
        if (v > '9')
            v += 'a' - ('9'+1);
        put_char(v);
    } while (--num_nibbles);
}

void put_hex_byte(uint8_t val)
{
    int num_nibbles = sizeof(val) * 2;

    do
    {
        char v = '0' + (((val >> (num_nibbles - 1) * 4)) & 0x0f);
        if (v > '9')
            v += 'a' - ('9'+1);
        put_char(v);
    } while (--num_nibbles);
}

void put_char(char character)
{
    if (character == '\n')
        add_char_to_buffer('\r');
    add_char_to_buffer(character);
}