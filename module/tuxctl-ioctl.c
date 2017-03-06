/* tuxctl-ioctl.c
 *
 * Driver (skeleton) for the mp2 tuxcontrollers for ECE391 at UIUC.
 *
 * Mark Murphy 2006
 * Andrew Ofisher 2007
 * Steve Lumetta 12-13 Sep 2009
 * Puskar Naha 2013
 */

#include <asm/current.h>
#include <asm/uaccess.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/miscdevice.h>
#include <linux/kdev_t.h>
#include <linux/tty.h>
#include <linux/spinlock.h>

#include "tuxctl-ld.h"
#include "tuxctl-ioctl.h"
#include "mtcp.h"

#define debug(str, ...) \
	printk(KERN_DEBUG "%s: " str, __FUNCTION__, ## __VA_ARGS__)

/************************ Protocol Implementation *************************/

// Spin lock for critical sections
spinlock_t tux_lock = SPIN_LOCK_UNLOCKED;

// The seven segment representation of each digit
char led_digits[16] = {
    0xE7,   // 0
    0x06,   // 1
    0xCB,   // 2
    0x8F,   // 3
    0x2E,   // 4
    0xAD,   // 5
    0xED,   // 6
    0x86,   // 7
    0xEF,   // 8
    0xAE,   // 9
    0xEE,   // A
    0x6D,   // B
    0xE1,   // C
    0x4F,   // D
    0xE9,   // E
    0xE8    // F
};

volatile char led_state[6] = {
    MTCP_LED_SET,
    0xf, 0, 0, 0, 0
};
volatile char button_state[2] = { 0 };

/* tuxctl_handle_packet()
 * IMPORTANT : Read the header for tuxctl_ldisc_data_callback() in
 * tuxctl-ld.c. It calls this function, so all warnings there apply
 * here as well.
 */
void tuxctl_handle_packet (struct tty_struct* tty, unsigned char* packet)
{
    unsigned op, b, c;
    unsigned long flags;
    char data;

    op = packet[0]; /* Avoid printk() sign extending the 8-bit */
    b = packet[1]; /* values when printing them. */
    c = packet[2];

    // prink("packet : %x %x %x\n", op, b, c);


    switch (op) {
        case MTCP_ACK:
            // prink("Tux ACK\n");
            break;
        case MTCP_BIOC_EVENT:
            // prink("Tux BIOC\n");
            button_state[0] = b;
            button_state[1] = c;
            break;
        case MTCP_RESET:
            // prink("Tux RESET\n");
            data = MTCP_BIOC_ON;
            tuxctl_ldisc_put(tty, &data, 1);
            data = MTCP_LED_USR;
            tuxctl_ldisc_put(tty, &data, 1);
            tuxctl_ldisc_put(tty, (char *)led_state, 6);
            break;
        case MTCP_POLL_OK:
            // prink("Tux POLL\n");
            break;
        default:
            break;
    }
}

int init(struct tty_struct *tty) {
    char cmd = MTCP_BIOC_ON;            // Enable button interrupts
    tuxctl_ldisc_put(tty, &cmd, 1);

    cmd = MTCP_LED_USR;                 // Enable LED user mode
    tuxctl_ldisc_put(tty, &cmd, 1);

    return 0;
}

int set_led(unsigned long arg, struct tty_struct *tty) {

    // Break up argument
    unsigned short display = (unsigned short) (arg & 0xffff);
    unsigned char digits_on = (unsigned char) ((arg & 0x0f0000) >> 16);
    unsigned char decimals_on = (unsigned char) ((arg & 0x0f000000) >> 24);

    int i;                              // Iteration  variable
    char cmd[6];                        // The command to send
    unsigned long flags;

    cmd[0] = MTCP_LED_SET;              // We're setting the LED's
    cmd[1] = 0xf;                       // All of them

    for (i = 0; i < 4; i++) {
        cmd[i + 2] = 0;                 // Initially clear LED

        // Set digit if on
        if (digits_on >> i & 0x1) {
            cmd[i + 2] = led_digits[display >> 4 * i & 0xf];
        }

        // Set decimal if on
        if (decimals_on >> i & 0x1) {
            cmd[i + 2] |= 0x10;
        }
    }

    // Save state in driver
    for (i = 0; i < 6; i++) {
        led_state[i] = cmd[i];
    }

    // Send to device
    tuxctl_ldisc_put(tty, cmd, 6);

    return 0;
}

int buttons(unsigned long arg, struct tty_struct *tty) {

    int error, data = 0;
    unsigned long flags;

    // Pack button data into an int
    data = button_state[0] & 0xf;
    data |= (button_state[1] & 0xf) << 4;

    // And copy that data to user space
    error = copy_to_user((int *)arg, &data, 4);

    return error ? -1 : 0;
}

/******** IMPORTANT NOTE: READ THIS BEFORE IMPLEMENTING THE IOCTLS ************
 *                                                                            *
 * The ioctls should not spend any time waiting for responses to the commands *
 * they send to the controller. The data is sent over the serial line at      *
 * 9600 BAUD. At this rate, a byte takes approximately 1 millisecond to       *
 * transmit; this means that there will be about 9 milliseconds between       *
 * the time you request that the low-level serial driver send the             *
 * 6-byte SET_LEDS packet and the time the 3-byte ACK packet finishes         *
 * arriving. This is far too long a time for a system call to take. The       *
 * ioctls should return immediately with success if their parameters are      *
 * valid.                                                                     *
 *                                                                            *
 ******************************************************************************/
int
tuxctl_ioctl (struct tty_struct* tty, struct file* file,
	      unsigned cmd, unsigned long arg)
{
    // prink("Tux ioctl called!\n");

    switch (cmd) {
    	case TUX_INIT:
            // prink("Tux INIT\n");
            return init(tty);
    	case TUX_BUTTONS:
            // prink("Tux BUTTONS\n");
            return buttons(arg, tty);
    	case TUX_SET_LED:
            // prink("Tux LED\n");
            return set_led(arg, tty);
    	default:
    	    return -EINVAL;
    }
}
