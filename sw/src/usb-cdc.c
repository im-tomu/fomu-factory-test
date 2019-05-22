#include <usb-cdc.h>

static int connected = 0;

int cdc_connected(void)
{
    return connected;
}

void cdc_set_connected(int is_connected)
{
    connected = is_connected;
}