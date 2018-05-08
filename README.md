# Linux_GPIO_Keyboard_Driver

A GPIO-based keyboard driver for embedded Linux devices. The driver enables 4 GPIO pins attached to push buttons and each pin has two states (i.e. two possible inputs). Pin 0-3 encodes key A, B, C, Entre, respectively under state 0; and X, Y, Z, Space, respectively under state 1. To toggle the state, just quickly press Pin 3 for 5 times.

To integrate the drive into the kernel, one can use the Yocto Project at: www.yoctoproject.org
