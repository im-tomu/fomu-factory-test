#include <tester.h>
#include <printf.h>
#include <spi.h>
#include <usb.h>
#include <fomu/csr.h>
#include <time.h>
#include <rgb.h>

int test_spi(void)
{
    uint8_t test_buffer[64];
    uint8_t compare_buffer[sizeof(test_buffer)];
    unsigned int i;
    int errors = 0;

    struct spi_id id = spiId();
    spiSetType(ST_QUAD);
    // printf("SPI Manufacturer: %02x\n", id.manufacturer_id);
    // printf("SPI Device ID: %02x\n", id.device_id);
    // printf("SPI Capacity: %02x %02x\n", id.memory_type, id.memory_size);

    for (i = 0; i < sizeof(test_buffer); i++) {
        test_buffer[i] = (i^0x9d) ^ (i<<5);
    }
    spiWrite(0, test_buffer, sizeof(test_buffer)-1);

    for (i = 0; i < sizeof(compare_buffer); i++) {
        compare_buffer[i] = 0;
    }
    spiRead(0, compare_buffer, sizeof(compare_buffer));

    for (i = 0; i < sizeof(compare_buffer); i++) {
        if (test_buffer[i] != compare_buffer[i]) {
            // printf("SPI: Offset %d  Expected %02x  Got %02x\n", i, test_buffer[i], compare_buffer[i]);
            errors++;
        }
    }

    return errors;
}

int test_led(void) {
    uint32_t pulses_per_second;
    touch_oe_write(touch_oe_read() & ~(1 << 1));
    // touch_oe_write(touch_oe_read() | (1 << 1));
    // touch_o_write(touch_o_read() & ~(1 << 1));
    rgb_bypass_write(1);
    rgb_mode_off();
    rgb_pwm_count_write(SYSTEM_CLOCK_FREQUENCY/1000*125);
    printf("Blinking: ");
    msleep(1000);
    pulses_per_second = rgb_pwm_count_read();
    rgb_pwm_count_write(SYSTEM_CLOCK_FREQUENCY/1000*125);
    printf("%08x / %08x / %08x\n", pulses_per_second, rgb_sent_pulses_read(), rgb_detected_pulses_read());
    return 0;
}

void tester_poll(void)
{
    int error_count = 0;
    printf("\nHello, world!\n");
    // error_count = test_spi();
    // printf("SPI errors: %d\n", error_count);
    while (1) {
        usb_poll();
        test_led();
    }
}