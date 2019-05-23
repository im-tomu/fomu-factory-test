#include <usb.h>
#include <usb-cdc.h>
#include <fomu/csr.h>

static int connected = 0;

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