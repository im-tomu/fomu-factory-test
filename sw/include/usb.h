#ifndef __USB_H
#define __USB_H

#ifdef __cplusplus
extern "C" {
#endif

struct usb_setup_request;

enum epfifo_response {
    EPF_ACK = 0,
    EPF_NAK = 1,
    EPF_NONE = 2,
    EPF_STALL = 3,
};

// Note that our PIDs are only bits 2 and 3 of the token,
// since all other bits are effectively redundant at this point.
enum USB_PID {
    USB_PID_OUT   = 0,
    USB_PID_SOF   = 1,
    USB_PID_IN    = 2,
    USB_PID_SETUP = 3,
};

#define USB_EV_ERROR 1
#define USB_EV_PACKET 2

void usb_isr(void);
void usb_init(void);
void usb_connect(void);
void usb_idle(void);
void usb_disconnect(void);

int usb_irq_happened(void);
int usb_setup(const struct usb_setup_request *setup);
void usb_send(const void *data, int total_count);
void usb_ack_in(void);
void usb_ack_out(void);
void usb_err(void);
int usb_recv(void *buffer, unsigned int buffer_len);
void usb_poll(void);
void usb_wait_for_send_done(void);

#ifdef __cplusplus
}
#endif

#endif