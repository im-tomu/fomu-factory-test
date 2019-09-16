#include <stdint.h>
#include <unistd.h>
#include <usb.h>

#include <usb-desc.h>
#include <usb-cdc.h>

static uint8_t reply_buffer[8];
static uint8_t usb_configuration = 0;

int usb_setup(const struct usb_setup_request *setup)
{
    const uint8_t *data = NULL;
    uint32_t datalen = 0;
    const usb_descriptor_list_t *list;
    uint32_t max_length = setup->wLength;//((setup->wLength >> 8) & 0xff) | ((setup->wLength << 8) & 0xff00);

    switch (setup->wRequestAndType)
    {

    // case 0x21a1: // Get Line Coding
    //     reply_buffer[0] = 0x80;
    //     reply_buffer[1] = 0x25;
    //     reply_buffer[2] = 0x00;
    //     reply_buffer[3] = 0x00;
    //     reply_buffer[4] = 0x00;
    //     reply_buffer[5] = 0x00;
    //     reply_buffer[6] = 0x08;
    //     data = reply_buffer;
    //     datalen = 7;
    //     break;

    case 0x2021: // Set Line Coding
    case 0x20A1: // Set Line Coding
        break;

    case 0x2221: // Set control line state
        cdc_set_connected(setup->wValue & 1); /* Check RTS bit */
        break;

    case 0x0500: // SET_ADDRESS
        usb_set_address(((uint8_t *)setup)[2]);
        break;

    case 0x0b01: // SET_INTERFACE
        break;

    case 0x0900: // SET_CONFIGURATION
        usb_configuration = setup->wValue;
        break;

    case 0x0880: // GET_CONFIGURATION
        reply_buffer[0] = usb_configuration;
        datalen = 1;
        data = reply_buffer;
        break;

    case 0x0080: // GET_STATUS (device)
        reply_buffer[0] = 0;
        reply_buffer[1] = 0;
        datalen = 2;
        data = reply_buffer;
        break;

    case 0x0082: // GET_STATUS (endpoint)
        if (setup->wIndex > 0)
        {
            usb_err();
            return 0;
        }
        reply_buffer[0] = 0;
        reply_buffer[1] = 0;

        // XXX handle endpoint stall here
        data = reply_buffer;
        datalen = 2;
        break;

    case 0x0102: // CLEAR_FEATURE (endpoint)
        if (setup->wIndex > 0 || setup->wValue != 0)
        {
            // TODO: do we need to handle IN vs OUT here?
            usb_err();
            return 0;
        }
        break;

    case 0x0302: // SET_FEATURE (endpoint)
        if (setup->wIndex > 0 || setup->wValue != 0)
        {
            // TODO: do we need to handle IN vs OUT here?
            usb_err();
            return 0;
        }
        // XXX: Should we set the stall bit?
        // USB->DIEP0CTL |= USB_DIEP_CTL_STALL;
        // TODO: do we need to clear the data toggle here?
        break;

    case 0x0680: // GET_DESCRIPTOR
    case 0x0681:
        for (list = usb_descriptor_list; 1; list++)
        {
            if (list->addr == NULL)
                break;
            if (setup->wValue == list->wValue)
            {
                data = list->addr;
                if ((setup->wValue >> 8) == 3)
                {
                    // for string descriptors, use the descriptor's
                    // length field, allowing runtime configured
                    // length.
                    datalen = *(list->addr);
                }
                else
                {
                    datalen = list->length;
                }
                goto send;
            }
        }
        usb_err();
        return 0;

    default:
        usb_err();
        return 0;
    }

send:
    if (data && datalen) {
        if (datalen > max_length)
            datalen = max_length;
        usb_send(data, datalen);
    }
    else
        usb_ack_in();
    return 2;
}
