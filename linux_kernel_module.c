/* Linux GPIO Keyboard Driver
 * Author: Jason Qi
 * UCL Electronic and Electrical Engineering
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/kthread.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/interrupt.h>
#include <linux/time.h>

#define    GPIO_IN_0        149          // GPIO_149
#define    GPIO_IN_1        140          // GPIO_140
#define    GPIO_IN_2        141          // GPIO_141
#define    GPIO_IN_3        142          // GPIO_142
#define    ROW               2           // number of rows of keymap i.e. number of states
#define    COLUMN            4           // number of columns of keymap i.e. number of GPIOs

static unsigned short state = 0;
static unsigned short pressCounter = 0;
static unsigned int lastInterruptTime = 0;
static unsigned int lastPressTime = 0;
static struct input_dev *input;
static int gpioMap[COLUMN] = {GPIO_IN_0, GPIO_IN_1, GPIO_IN_2, GPIO_IN_3};      // map of GPIOs
static unsigned short keyMap[ROW][COLUMN] = {KEY_A, KEY_B, KEY_C, KEY_ENTER,    // keys for state 0
                                            KEY_X, KEY_Y, KEY_Z, KEY_SPACE};  // keys for state 1

// Return current time in milliseconds
inline static unsigned int getTimeMillis (void)
{
    // Instantiate a time interval struct
    struct timeval timeInterval;
    uint64_t now = 0;
    
    // Get current time
    do_gettimeofday(&timeInterval);
    
    // Convert units into milliseconds
    now  = (uint64_t)timeInterval.tv_sec * (uint64_t)1000 + (uint64_t)(timeInterval.tv_usec / 1000);
    
    return (uint32_t)now;
}


// Send a key event to kernel
inline static void sendKey(unsigned short row, unsigned short column)
{
    input_report_key(input, keyMap[row][column], 1);
    input_sync(input);
    input_report_key(input, keyMap[row][column], 0);
    input_sync(input);
}


// General interrupt service routine for key events
inline static irq_handler_t InterruptHandler_general(unsigned int irq, struct pt_regs *regs, unsigned short column )
{
    unsigned long flags;
    unsigned int interruptTime = getTimeMillis();

    // Ignore the interrupts due to bouncing
    if (interruptTime - lastInterruptTime < 250)
        return (irq_handler_t)IRQ_HANDLED;

    // Disable further interrupts
    local_irq_save(flags);
    sendKey(state, column);
    lastInterruptTime = interruptTime;
    pressCounter=0;

     // Restore interrupts, update interrupt time
    local_irq_restore(flags);
  
    return (irq_handler_t)IRQ_HANDLED;
}


// Interrupt service routine to handle GPIO_IN_0
static irq_handler_t InterruptHandler_0(unsigned int irq, struct pt_regs *regs )
{
    return InterruptHandler_general(irq, regs, 0);
}


// Interrupt service routine to handle GPIO_IN_1
static irq_handler_t InterruptHandler_1(unsigned int irq, struct pt_regs *regs )
{
    return InterruptHandler_general(irq, regs, 1);
}


// Interrupt service routine to handle GPIO_IN_2
static irq_handler_t InterruptHandler_2(unsigned int irq, struct pt_regs *regs )
{
    return InterruptHandler_general(irq, regs, 2);
}


// Interrupt service routine to handle GPIO_IN_3
static irq_handler_t InterruptHandler_3(unsigned int irq, struct pt_regs *regs )
{
    unsigned long flags;
    unsigned int interruptTime = getTimeMillis();

    // Ignore the interrupts due to bouncing
    if (interruptTime - lastInterruptTime < 80)
        return (irq_handler_t)IRQ_HANDLED;

    // Disable hard interrupts on the local CPU, and restore them
    local_irq_save(flags);

    // Determine state
    if (interruptTime - lastPressTime < 350)
    {
        // The state toggles when GPIO_IN 3 is pressed 5 times in a row
        if (pressCounter > 6)
        {
            // Toggle the current key state
            state ^= (unsigned short)1;
            pressCounter = 0;
            lastPressTime = interruptTime;
            local_irq_restore(flags);
            return (irq_handler_t)IRQ_HANDLED;
        }
        ++pressCounter;
    }
    
    // Press number less than 6, send key even as usual
    else
    {
        pressCounter = 0;
        sendKey(state, 3);
    }
  
    lastPressTime = interruptTime;
  
    // Restore hard interrupts
    local_irq_restore(flags);
  
    return (irq_handler_t)IRQ_HANDLED;
}


// Kernel module intialisation function
static int keyboard_init(void)
{
    // Dummy temporary variables
    int returnValue = 0;
    int error = 0, i = 0, j = 0;

    // Initialise the GPIOs
    for (i = 0; i < COLUMN; i++)
    {
        if (gpio_request(gpioMap[i], "GPIO_Keyboard"))
        {
            printk (KERN_INFO "GPIO_Keyboard: %s unable to request GPIO\n", __func__ );
            returnValue = -EBUSY;
            goto DONE;
        }

        if (gpio_direction_input(gpioMap[i]) < 0 )
        {
            printk (KERN_INFO "GPIO_Keyboard: %s unable to set GPIO_IN as input\n", __func__ );
            returnValue = -EBUSY;
            goto DONE;
        }
    }

    // Allocate to the input device structure
    input = input_allocate_device();
    if (!input)
        error = -ENOMEM;
    input->name = "GPIO_Keyboard";
    input->keycode = keyMap;
    input->keycodesize = sizeof(unsigned short);
    input->keycodemax = ROW * COLUMN;
    input->evbit[0] = BIT(EV_KEY);

    // Register all the keybits one by one
    for (i = 0; i < ROW; i++)
        for (j = 0; j < COLUMN; j++)
            __set_bit(keyMap[i][j], input->keybit);

    // Register input device
    error = input_register_device(input);
    if (error)
    {
        printk(KERN_CRIT "input_register_device failed");
        goto DONE;
    }

    // Defined an array of declared interrupt handler pointers
    irq_handler_t* InterruptHandlerMap [COLUMN] = {InterruptHandler_0, InterruptHandler_1, InterruptHandler_2, InterruptHandler_3};

    // Request interrupts for GPIOs
    for (i = 0; i < COLUMN; i++)
    {
        if (request_irq(gpio_to_irq(gpioMap[i]), (irq_handler_t)InterruptHandlerMap[i], IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "GPIO_Keyboard", NULL) < 0)
        {
            printk(KERN_INFO "GPIO_Keyboard: %s unable to register GPIO IRQ for GPIO_IN\n", __func__ );
            returnValue = -EBUSY;
            goto DONE;
        }
    }
    
    printk(KERN_NOTICE "Keyboard driver initialised\n");

    DONE:
    return returnValue;
}


// Kernel module exit function
static void keyboard_exit(void)
{
    short i = 0;
    
    // Unregister the virtual input device
    input_unregister_device(input);
    input_free_device(input);

    // Free GPIOs and interrupts
    for (i=0; i<COLUMN; i++)
    {
        free_irq(gpio_to_irq (gpioMap[i]), NULL);
        gpio_free(gpioMap[i]);
    }

    printk(KERN_NOTICE "GPIO Keyboard driver exit\n");
}


module_init(keyboard_init);
module_exit(keyboard_exit);


MODULE_LICENSE ("GPL");
MODULE_AUTHOR ("Jason Qi");
MODULE_DESCRIPTION ("Linux driver for a simple external keyboard");
