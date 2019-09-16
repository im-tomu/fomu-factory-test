#include <irq.h>
#include <riscv.h>
#include <string.h>
#include <usb.h>
#include <fomu/csr.h>

#ifdef CSR_USB_EP_0_OUT_EV_PENDING_ADDR

static const int max_byte_length = 64;

#define EP2OUT_BUFFERS 4
#define EP0OUT_BUFFER_SIZE 256
__attribute__((aligned(4)))
static uint8_t volatile usb_ep0out_buffer[64 + 2];
static int wait_reply;
static int wait_type;

#define EP2OUT_BUFFER_SIZE 256
static uint8_t volatile usb_ep2out_buffer_len[EP2OUT_BUFFERS];
static uint8_t volatile usb_ep2out_buffer[EP2OUT_BUFFERS][EP2OUT_BUFFER_SIZE];
static volatile uint8_t usb_ep2out_wr_ptr;
static volatile uint8_t usb_ep2out_rd_ptr;

static const uint8_t * volatile current_data;
static volatile int current_length;
static volatile int data_offset;
static volatile int data_to_send;
static int next_packet_is_empty;

static volatile int have_new_address;
static volatile uint8_t new_address;

// Firmware versions < 1.9 didn't have usb_address_write()
static inline void usb_set_address_wrapper(uint8_t address) {
    usb_address_write(address);
}

void usb_idle(void) {
    usb_ep_0_out_ev_enable_write(0);
    usb_ep_0_in_ev_enable_write(0);

    // Reject all incoming data, since there is no handler anymore
    usb_ep_0_out_respond_write(EPF_NAK);

    // Reject outgoing data, since we don't have any to give.
    usb_ep_0_in_respond_write(EPF_NAK);

    irq_setmask(irq_getmask() & ~(1 << USB_INTERRUPT));
}

void usb_disconnect(void) {
    usb_ep_0_out_ev_enable_write(0);
    usb_ep_0_in_ev_enable_write(0);
    irq_setmask(irq_getmask() & ~(1 << USB_INTERRUPT));
    usb_pullup_out_write(0);
    usb_set_address_wrapper(0);
}

void usb_connect(void) {
    usb_set_address_wrapper(0);
    usb_ep_0_out_ev_pending_write(usb_ep_0_out_ev_enable_read());
    usb_ep_0_in_ev_pending_write(usb_ep_0_in_ev_pending_read());
    usb_ep_0_out_ev_enable_write(USB_EV_PACKET | USB_EV_ERROR);
    usb_ep_0_in_ev_enable_write(USB_EV_PACKET | USB_EV_ERROR);

    usb_ep_1_in_ev_pending_write(usb_ep_1_in_ev_enable_read());
    usb_ep_1_in_ev_enable_write(USB_EV_PACKET | USB_EV_ERROR);

    usb_ep_2_out_ev_pending_write(usb_ep_2_out_ev_enable_read());
    usb_ep_2_in_ev_pending_write(usb_ep_2_in_ev_pending_read());
    usb_ep_2_out_ev_enable_write(USB_EV_PACKET | USB_EV_ERROR);
    usb_ep_2_in_ev_enable_write(USB_EV_PACKET | USB_EV_ERROR);

    // Accept incoming data by default.
    usb_ep_0_out_respond_write(EPF_ACK);
    usb_ep_2_out_respond_write(EPF_ACK);

    // Reject outgoing data, since we have none to give yet.
    usb_ep_0_in_respond_write(EPF_NAK);

    usb_pullup_out_write(1);

	irq_setmask(irq_getmask() | (1 << USB_INTERRUPT));
}

void usb_init(void) {
    // usb_ep0out_wr_ptr = 0;
    // usb_ep0out_rd_ptr = 0;
    usb_pullup_out_write(0);
    return;
}

static void process_tx(void) {

    // Don't allow requeueing -- only queue more data if we're
    // currently set up to respond NAK.
    if (usb_ep_0_in_respond_read() != EPF_NAK) {
        return;
    }

    // Prevent us from double-filling the buffer.
    if (!usb_ep_0_in_ibuf_empty_read()) {
        return;
    }

    if (!current_data || !current_length) {
        return;
    }

    data_offset += data_to_send;

    data_to_send = current_length - data_offset;

    // Clamp the data to the maximum packet length
    if (data_to_send > max_byte_length) {
        data_to_send = max_byte_length;
        next_packet_is_empty = 0;
    }
    else if (data_to_send == max_byte_length) {
        next_packet_is_empty = 1;
    }
    else if (next_packet_is_empty) {
        next_packet_is_empty = 0;
        data_to_send = 0;
    }
    else if (current_data == NULL || data_to_send <= 0) {
        next_packet_is_empty = 0;
        current_data = NULL;
        current_length = 0;
        data_offset = 0;
        data_to_send = 0;
        return;
    }

    int this_offset;
    for (this_offset = data_offset; this_offset < (data_offset + data_to_send); this_offset++) {
        usb_ep_0_in_ibuf_head_write(current_data[this_offset]);
    }
    usb_ep_0_in_respond_write(EPF_ACK);
    return;
}

void usb_send(const void *data, int total_count) {
    while ((current_length || current_data) && (usb_ep_0_in_respond_read() != EPF_NAK))
        ;
    current_data = (uint8_t *)data;
    current_length = total_count;
    data_offset = 0;
    data_to_send = 0;
    process_tx();
}

void usb_wait_for_send_done(void) {
    while (current_data && current_length)
        usb_poll();
    while ((usb_ep_0_in_dtb_read() & 1) == 1)
        usb_poll();
}

void usb_isr(void) {
    uint8_t ep0out_pending = usb_ep_0_out_ev_pending_read();
    uint8_t ep0in_pending = usb_ep_0_in_ev_pending_read();
    uint8_t ep1in_pending = usb_ep_1_in_ev_pending_read();
    uint8_t ep2in_pending = usb_ep_2_in_ev_pending_read();
    uint8_t ep2out_pending = usb_ep_2_out_ev_pending_read();

    // We just got an "IN" token.  Send data if we have it.
    if (ep0in_pending) {
        if (wait_reply == 2) {
            wait_reply--;
            if (!wait_type) {
                wait_type = 1;
            }
        }
        else if (wait_reply == 1) {
            if (wait_type == 2) {
                current_data = NULL;
                current_length = 0;
            }
            wait_type = 0;
        }
        usb_ep_0_in_respond_write(EPF_NAK);
        usb_ep_0_in_ev_pending_write(ep0in_pending);
        if (have_new_address) {
            have_new_address = 0;
            usb_set_address_wrapper(new_address);
        }
    }

    if (ep1in_pending) {
        usb_ep_1_in_respond_write(EPF_NAK);
        usb_ep_1_in_ev_pending_write(ep1in_pending);
    }

    if (ep2in_pending) {
        usb_ep_2_in_respond_write(EPF_NAK);
        usb_ep_2_in_ev_pending_write(ep2in_pending);
    }

    if (ep2out_pending) {
#ifdef LOOPBACK_TEST
        volatile uint8_t * obuf = usb_ep2out_buffer[usb_ep2out_wr_ptr];
        int sz = 0;

        if (wait_reply == 2) {
            wait_reply--;
            wait_type = 2;
        }
        else if (wait_reply == 1) {
            wait_reply--;
        }
        while (!usb_ep_2_out_obuf_empty_read()) {
            if (sz < EP2OUT_BUFFER_SIZE)
                obuf[sz++] = usb_ep_2_out_obuf_head_read() + 1;
            usb_ep_2_out_obuf_head_write(0);
        }
        if (sz > 2) {
            usb_ep2out_buffer_len[usb_ep2out_wr_ptr] = sz - 2; /* Strip off CRC16 */
            usb_ep2out_wr_ptr = (usb_ep2out_wr_ptr + 1) & (EP2OUT_BUFFERS-1);
        }
#else // !LOOPBACK_TEST
        while (!usb_ep_2_out_obuf_empty_read()) {
            usb_ep_2_out_obuf_head_write(0);
        }
#endif
        usb_ep_2_out_respond_write(EPF_ACK);
        usb_ep_2_out_ev_pending_write(ep2out_pending);
    }

    // We got an OUT or a SETUP packet.  Copy it to usb_ep0out_buffer
    // and clear the "pending" bit.
    if (ep0out_pending) {
        unsigned int byte_count = 0;
        for (byte_count = 0; byte_count < sizeof(usb_ep0out_buffer); byte_count++)
            usb_ep0out_buffer[byte_count] = '\0';

        byte_count = 0;
        while (!usb_ep_0_out_obuf_empty_read()) {
            uint8_t byte = usb_ep_0_out_obuf_head_read();
            usb_ep_0_out_obuf_head_write(0);
            usb_ep0out_buffer[byte_count++] = byte;
        }

        if (byte_count >= 2) {
            volatile void *setup_buffer = usb_ep0out_buffer;
            usb_ep_0_in_dtb_write(1);
            data_offset = 0;
            current_length = 0;
            current_data = NULL;
            byte_count -= 2;
            // XXX TERRIBLE HACK!
            // Because the epfifo backend doesn't have any concept of packet boundaries,
            // sometimes one or two of the bytes from the CRC on the "ACK" from the previous
            // "Get Descriptor" will be stuck on the front of this request.
            // This can happen if, for example, we get the OUT from that and the OUT from
            // the subsequent SETUP packet without first handling that.
            // Since all SETUP packets are 8 bytes (in this tester), we'll simply clamp the
            // SETUP data packet to be the last 8 bytes received (minus the 2-byte CRC16).
            // This is horrible and should be fixed in hardware.
            if (byte_count > 8)
                setup_buffer += byte_count - 8;
            wait_reply = usb_setup((const struct usb_setup_request *)setup_buffer);
        }
        usb_ep_0_out_ev_pending_write(ep0out_pending);
        usb_ep_0_out_respond_write(EPF_ACK);
    }

    process_tx();
}

void usb_ack_in(void) {
    while (usb_ep_0_in_respond_read() == EPF_ACK)
        ;
    usb_ep_0_in_respond_write(EPF_ACK);
}

void usb_ack_out(void) {
    while (usb_ep_0_out_respond_read() == EPF_ACK)
        ;
    usb_ep_0_out_respond_write(EPF_ACK);
}

void usb_err(void) {
    usb_ep_0_out_respond_write(EPF_STALL);
    usb_ep_0_in_respond_write(EPF_STALL);
}

#if 0
int usb_recv(void *buffer, unsigned int buffer_len) {

    // Set the OUT response to ACK, since we are in a position to receive data now.
    usb_ep_0_out_respond_write(EPF_ACK);
    while (1) {
        if (usb_ep0out_rd_ptr != usb_ep0out_wr_ptr) {
            if (usb_ep0out_last_tok[usb_ep0out_rd_ptr] == USB_PID_OUT) {
                unsigned int ep0_buffer_len = usb_ep0out_buffer_len[usb_ep0out_rd_ptr];
                if (ep0_buffer_len < buffer_len)
                    buffer_len = ep0_buffer_len;
                // usb_ep0out_buffer_len[usb_ep0out_rd_ptr] = 0;
                memcpy(buffer, (void *)&usb_ep0out_buffer[usb_ep0out_rd_ptr], buffer_len);
                usb_ep0out_rd_ptr = (usb_ep0out_rd_ptr + 1) & (EP0OUT_BUFFERS-1);
                return buffer_len;
            }
            usb_ep0out_rd_ptr = (usb_ep0out_rd_ptr + 1) & (EP0OUT_BUFFERS-1);
        }
    }
    return 0;
}
#endif

void usb_set_address(uint8_t new_address_) {
    new_address = new_address_;
    have_new_address = 1;
}

void usb_poll(void) {
    process_tx();
#ifdef LOOPBACK_TEST
    if (usb_ep2out_rd_ptr != usb_ep2out_wr_ptr) {
        volatile uint8_t *buf = usb_ep2out_buffer[usb_ep2out_rd_ptr];
        unsigned int len = usb_ep2out_buffer_len[usb_ep2out_rd_ptr];
        unsigned int i;
        while (usb_ep_2_in_respond_read() == EPF_ACK) {
            ;
        }
        for (i = 0; i < len; i++) {
            usb_ep_2_in_ibuf_head_write(buf[i]);
        }
        usb_ep_2_in_respond_write(EPF_ACK);
        usb_ep2out_rd_ptr = (usb_ep2out_rd_ptr + 1) & (EP2OUT_BUFFERS-1);
    }
#endif
}

#endif /* CSR_USB_EP_0_OUT_EV_PENDING_ADDR */