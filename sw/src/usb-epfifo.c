#include <irq.h>
#include <riscv.h>
#include <string.h>
#include <usb.h>

#ifdef CSR_USB_EP_0_OUT_EV_PENDING_ADDR

static const int max_byte_length = 64;

#define EP0OUT_BUFFERS 4
#define EP2OUT_BUFFERS 4
__attribute__((aligned(4)))
#define EP0OUT_BUFFER_SIZE 256
static uint8_t volatile usb_ep0out_buffer_len[EP0OUT_BUFFERS];
static uint8_t volatile usb_ep0out_buffer[EP0OUT_BUFFERS][EP0OUT_BUFFER_SIZE];
static uint8_t volatile usb_ep0out_last_tok[EP0OUT_BUFFERS];
static volatile uint8_t usb_ep0out_wr_ptr;
static volatile uint8_t usb_ep0out_rd_ptr;

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
}

void usb_connect(void) {
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
    usb_ep0out_wr_ptr = 0;
    usb_ep0out_rd_ptr = 0;
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

    while ((current_length || current_data))// && usb_ep_0_in_respond_read() != EPF_NAK)
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

    // We got an OUT or a SETUP packet.  Copy it to usb_ep0out_buffer
    // and clear the "pending" bit.
    if (ep0out_pending) {
        uint8_t last_tok = usb_ep_0_out_last_tok_read();
        
        int byte_count = 0;
        usb_ep0out_last_tok[usb_ep0out_wr_ptr] = last_tok;
        volatile uint8_t * obuf = usb_ep0out_buffer[usb_ep0out_wr_ptr];
        while (!usb_ep_0_out_obuf_empty_read()) {
            obuf[byte_count++] = usb_ep_0_out_obuf_head_read();
            usb_ep_0_out_obuf_head_write(0);
        }
        if (byte_count >= 2)
            usb_ep0out_buffer_len[usb_ep0out_wr_ptr] = byte_count - 2 /* Strip off CRC16 */;
        usb_ep0out_wr_ptr = (usb_ep0out_wr_ptr + 1) & (EP0OUT_BUFFERS-1);

        if (last_tok == USB_PID_SETUP) {
            usb_ep_0_in_dtb_write(1);
            data_offset = 0;
            current_length = 0;
            current_data = NULL;
        }
        usb_ep_0_out_ev_pending_write(ep0out_pending);
        usb_ep_0_out_respond_write(EPF_ACK);
    }

    // We just got an "IN" token.  Send data if we have it.
    if (ep0in_pending) {
        usb_ep_0_in_respond_write(EPF_NAK);
        usb_ep_0_in_ev_pending_write(ep0in_pending);
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
        volatile uint8_t * obuf = usb_ep2out_buffer[usb_ep2out_wr_ptr];
        int sz = 0;
        while (!usb_ep_2_out_obuf_empty_read()) {
            if (sz < EP2OUT_BUFFER_SIZE)
                obuf[sz++] = usb_ep_2_out_obuf_head_read() + 1;
            usb_ep_2_out_obuf_head_write(0);
        }
        if (sz > 2) {
            usb_ep2out_buffer_len[usb_ep2out_wr_ptr] = sz - 2; /* Strip off CRC16 */
            usb_ep2out_wr_ptr = (usb_ep2out_wr_ptr + 1) & (EP2OUT_BUFFERS-1);
        }

        usb_ep_2_out_respond_write(EPF_ACK);
        usb_ep_2_out_ev_pending_write(ep2out_pending);
    }

    return;
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

void usb_poll(void) {
    // If some data was received, then process it.
    while (usb_ep0out_rd_ptr != usb_ep0out_wr_ptr) {
        const struct usb_setup_request *request = (const struct usb_setup_request *)(usb_ep0out_buffer[usb_ep0out_rd_ptr]);
        // uint8_t len = usb_ep0out_buffer_len[usb_ep0out_rd_ptr];
        uint8_t last_tok = usb_ep0out_last_tok[usb_ep0out_rd_ptr];

        // usb_ep0out_buffer_len[usb_ep0out_rd_ptr] = 0;
        usb_ep0out_rd_ptr = (usb_ep0out_rd_ptr + 1) & (EP0OUT_BUFFERS-1);

        if (last_tok == USB_PID_SETUP) {
            usb_setup(request);
        }
    }

    process_tx();
}

#endif /* CSR_USB_EP_0_OUT_EV_PENDING_ADDR */