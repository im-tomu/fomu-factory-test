#include <stdio.h>
#include <irq.h>
#include <usb.h>
#include <time.h>
#include <rgb.h>
#include <spi.h>
#include <fomu/csr.h>

struct ff_spi *spi;

void isr(void)
{
    unsigned int irqs;

    irqs = irq_pending() & irq_getmask();

    if (irqs & (1 << USB_INTERRUPT))
        usb_isr();
}

static void init(void)
{
    rgb_init();
    spi = spiAlloc();
    spiInit(spi);
    irq_setmask(0);
    irq_setie(1);
    usb_init();
    time_init();

}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    init();

    usb_connect();
    while (1)
    {
        usb_poll();
    }
    return 0;
}
