#include <tester.h>
#include <usb-cdc.h>
#include <spi.h>
#include <usb.h>
#include <fomu/csr.h>
#include <time.h>
#include <rgb.h>

static uint32_t test_spi(void)
{
    uint8_t test_buffer[64];
    uint8_t compare_buffer[sizeof(test_buffer)];
    unsigned int i;
    int errors = 0;

    struct spi_id id = spiId();
    spiSetType(ST_QUAD);
    put_string("SPI: Manufacturer ");
    put_hex_byte(id.manufacturer_id);
    put_string(" / ");

    put_string("Device ID ");
    put_hex_byte(id.device_id);
    put_string(" / ");

    put_string("Capacity ");
    put_hex_byte(id.memory_type);
    put_char(' ');
    put_hex_byte(id.memory_size);
    put_string(" / ");

    put_string("Serial ");
    put_hex(*((uint32_t *)id.serial));
    put_string(" / ");

    for (i = 0; i < sizeof(test_buffer); i++)
    {
        test_buffer[i] = (i ^ 0x9d) ^ (i << 5);
    }
    spiWrite(0, test_buffer, sizeof(test_buffer));

    for (i = 0; i < sizeof(compare_buffer); i++)
    {
        compare_buffer[i] = 0;
    }
    spiRead(0, compare_buffer, sizeof(compare_buffer));

    for (i = 0; i < sizeof(compare_buffer); i++)
    {
        if (test_buffer[i] != compare_buffer[i])
        {
            put_string("E@");
            put_hex_byte(i);
            put_char(':');
            put_hex_byte(test_buffer[i]);
            put_char('!');
            put_hex_byte(compare_buffer[i]);
            put_char(' ');
            // printf("SPI: Offset %d  Expected %02x  Got %02x\n", i, test_buffer[i], compare_buffer[i]);
            errors++;
        }
    }
    if (!errors)
    {
        put_string("Pass\n");
    }
    else
    {
        put_string("FAIL\n");
    }

    return errors;
}

static uint32_t test_one_pad(uint8_t src, uint8_t dest)
{
    unsigned int loops;
    unsigned int matches = 0;
    const unsigned int loop_max = 10;

    put_char('0' + src);
    put_char('>');
    put_char('0' + dest);
    put_char(':');
    for (loops = 0; loops < loop_max; loops++)
    {
        // Set pin 2 as output, and pin 0 as input, and see if it loops back.
        touch_oe_write((1 << src) | (0 << dest));
        touch_o_write((loops & 1) << src);
        if ((loops & 1) == !!((touch_i_read() & (1 << dest))))
            matches++;
    }
    if (matches == loop_max)
    {
        put_string("OK ");
        return 0;
    }
    else
    {
        put_string("FAIL(");
        put_hex_byte(loop_max);
        put_char('!');
        put_hex_byte(matches);
        put_string(") ");
        return (loop_max - matches);
    }
}

static uint32_t test_touch(void)
{
    uint32_t error_count = 0;

    put_string("TOUCH: ");

    error_count += test_one_pad(0, 2);
    error_count += test_one_pad(0, 3);
    error_count += test_one_pad(2, 0);
    error_count += test_one_pad(2, 3);
    error_count += test_one_pad(3, 0);
    error_count += test_one_pad(3, 2);

    if (error_count)
        put_string("FAIL\n");
    else
        put_string("Pass\n");
    return error_count;
}

static const char color_names[] = {'B', 'R', 'G'};

static uint32_t test_one_color(int color)
{
    uint32_t pulses_per_second;
    uint32_t sent_pulses;
    uint32_t detected_pulses;
    uint32_t high_value;

    rgb_bypass_write(1 << color);
    rgb_mode_off();

    // Amount of time it's off (900 us)
    rgb_duty_write(SYSTEM_CLOCK_FREQUENCY / 10000 * 9);

    // Total amount of time for one pulse (1000 us or 1 ms)
    rgb_pulse_write(SYSTEM_CLOCK_FREQUENCY / 10000 * 10);

    put_string("RGB");
    put_char(color_names[color]);
    put_string(": ");
    msleep(100);

    rgb_pulse_write(SYSTEM_CLOCK_FREQUENCY / 1000 * 1);
    pulses_per_second = rgb_pulse_read();
    high_value = rgb_duty_read();
    sent_pulses = rgb_sent_pulses_read();
    detected_pulses = rgb_detected_pulses_read();

    put_hex(pulses_per_second);
    put_string(" / ");
    put_hex(high_value);
    put_string(" / ");
    put_hex(sent_pulses);
    put_string(" / ");
    put_hex(detected_pulses);
    put_string(" / ");
    rgb_bypass_write(0);

    uint32_t ratio = ((detected_pulses * 100) / sent_pulses);
    put_string("Ratio: 0x");
    put_hex_byte(ratio);
    put_string(" / ");
    if (ratio > 60)
    {
        put_string("Pass\n");
        return 0;
    }

    put_string("FAIL\n");
    return 1 + ratio;
}

static uint32_t test_led(void)
{
    uint32_t error_count = 0;

    touch_oe_write(touch_oe_read() & ~(1 << 1));
    // touch_oe_write(touch_oe_read() | (1 << 1));
    // touch_o_write(touch_o_read() & ~(1 << 1));

    error_count += test_one_color(0);
    error_count += test_one_color(1);
    error_count += test_one_color(2);
    return error_count;
}

void tester_poll(void)
{
    int error_count = 0;
    put_char('\n');
    flush_serial();
    put_string("\nFomu Tester " GIT_VERSION "\n");
    error_count += test_spi();
    error_count += test_led();
    error_count += test_touch();

    put_string("FOMU: (0x");
    put_hex(error_count);
    put_string(" errors) ");
    if (error_count)
        put_string("FAIL!\n");
    else
        put_string("ALL_PASS\n");
    while (1)
    {
        usb_poll();
    }
}