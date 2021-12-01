#include "MK60D10.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "display.h"
#include "message.h"

/* Macros for bit-level registers manipulation */
#define GPIO_PIN_MASK    0x1Fu
#define GPIO_PIN(x)        (((1)<<(x & GPIO_PIN_MASK)))

#define BTN_SW3 0x1000    // Port E, bit 12
#define BTN_SW5 0x4000000 // Port E, bit 26

/* Global struct to access in handlers */
display_struct *display = NULL;
message_struct *message = NULL;

/* Pins mapping */
uint8_t row_pins[rows] = {26, 24, 9, 25, 28, 7, 27, 29};

/* Configuration of the necessary MCU peripherals */
void SystemConfig() {
    /* Set system clock and disable watchdog */
    MCG_C4 |= (MCG_C4_DMX32_MASK | MCG_C4_DRST_DRS(0x01));
    SIM_CLKDIV1 |= SIM_CLKDIV1_OUTDIV1(0x00);
    WDOG_STCTRLH &= ~WDOG_STCTRLH_WDOGEN_MASK;

    /* Turn on all port clocks */
    SIM->SCGC5 = SIM_SCGC5_PORTA_MASK | SIM_SCGC5_PORTE_MASK;
    SIM->SCGC6 |= SIM_SCGC6_PIT_MASK;

    /* Set corresponding PTA pins (column activators of 74HC154) for GPIO functionality */
    PORTA->PCR[8] = (0 | PORT_PCR_MUX(0x01));  // A0
    PORTA->PCR[10] = (0 | PORT_PCR_MUX(0x01)); // A1
    PORTA->PCR[6] = (0 | PORT_PCR_MUX(0x01));  // A2
    PORTA->PCR[11] = (0 | PORT_PCR_MUX(0x01)); // A3

    /* Set corresponding PTA pins (rows selectors of 74HC154) for GPIO functionality */
    PORTA->PCR[26] = (0 | PORT_PCR_MUX(0x01));  // R0
    PORTA->PCR[24] = (0 | PORT_PCR_MUX(0x01));  // R1
    PORTA->PCR[9] = (0 | PORT_PCR_MUX(0x01));   // R2
    PORTA->PCR[25] = (0 | PORT_PCR_MUX(0x01));  // R3
    PORTA->PCR[28] = (0 | PORT_PCR_MUX(0x01));  // R4
    PORTA->PCR[7] = (0 | PORT_PCR_MUX(0x01));   // R5
    PORTA->PCR[27] = (0 | PORT_PCR_MUX(0x01));  // R6
    PORTA->PCR[29] = (0 | PORT_PCR_MUX(0x01));  // R7

    /* Set buttons */
    PORTE->PCR[12] = PORT_PCR_MUX(0x01); // SW3
    PORTE->PCR[26] = PORT_PCR_MUX(0x01); // SW5

    /* Set corresponding PTE pins (output enable of 74HC154) for GPIO functionality */
    PORTE->PCR[28] = (0 | PORT_PCR_MUX(0x01)); // #EN

    /* Change corresponding PTA port pins as outputs */
    PTA->PDDR = GPIO_PDDR_PDD(0x3F000FC0);

    /* Change corresponding PTE port pins as outputs */
    PTE->PDDR = GPIO_PDDR_PDD(GPIO_PIN(28));

    /* Enable PIT0 */
    PIT_MCR = 0;
    PIT_TCTRL0 = (0 | PIT_TCTRL_TEN_MASK | PIT_TCTRL_TIE_MASK);
    /* 4KHz renew column */
    PIT_LDVAL0 = 11999;
    PIT_TFLG0 |= PIT_TFLG_TIF_MASK;

    /* Enable PIT1 */
    PIT_TCTRL1 = (0 | PIT_TCTRL_TEN_MASK | PIT_TCTRL_TIE_MASK);
    /* 4 Hz move message*/
    PIT_LDVAL1 = 11999999;
    PIT_TFLG1 |= PIT_TFLG_TIF_MASK;

}

/* Enable PIT Interrupts */
void enable_interrupts() {
    NVIC_ClearPendingIRQ(PIT0_IRQn);
    NVIC_EnableIRQ(PIT0_IRQn);

    NVIC_ClearPendingIRQ(PIT1_IRQn);
    NVIC_EnableIRQ(PIT1_IRQn);
}

/* Actualize display values */
void actualize_values(unsigned long message_length, uint8_t char_set_index, unsigned long index) {
    /* We run out of chars, we clean display by setting 0 everywhere */
    if (index / char_width_with_space >= message_length) {
        for (int j = 0; j < columns - 1; j++) {
            display->display_values[j] = display->display_values[j + 1];
        }
        display->display_values[last_column] = 0;
    }
    /* We have chars to read */
    else {
        /* We shift each value to actualize display */
        for (int j = 0; j < columns - 1; j++) {
            display->display_values[j] = display->display_values[j + 1];
        }
        /* We add space or character column specified in char_set depending on current index */
        if (index % char_width_with_space < 4) {
            display->display_values[last_column] = char_set[char_set_index][index % char_width_with_space];
        } else {
            display->display_values[last_column] = 0;
        }
    }
}


/* Conversion of requested column number into the 4-to-16 decoder control. */
void column_select(unsigned int col_num) {
    unsigned i, result, col_sel[4];

    for (i = 0; i < 4; i++) {
        result = col_num / 2;      // Whole-number division of the input number
        col_sel[i] = col_num % 2;
        col_num = result;

        switch (i) {
            // Selection signal A0
            case 0:
                ((col_sel[i]) == 0) ? (PTA->PDOR &= ~GPIO_PDOR_PDO(GPIO_PIN(8))) : (PTA->PDOR |= GPIO_PDOR_PDO(
                        GPIO_PIN(8)));
                break;

            // Selection signal A1
            case 1:
                ((col_sel[i]) == 0) ? (PTA->PDOR &= ~GPIO_PDOR_PDO(GPIO_PIN(10))) : (PTA->PDOR |= GPIO_PDOR_PDO(
                        GPIO_PIN(10)));
                break;

            // Selection signal A2
            case 2:
                ((col_sel[i]) == 0) ? (PTA->PDOR &= ~GPIO_PDOR_PDO(GPIO_PIN(6))) : (PTA->PDOR |= GPIO_PDOR_PDO(
                        GPIO_PIN(6)));
                break;

            // Selection signal A3
            case 3:
                ((col_sel[i]) == 0) ? (PTA->PDOR &= ~GPIO_PDOR_PDO(GPIO_PIN(11))) : (PTA->PDOR |= GPIO_PDOR_PDO(
                        GPIO_PIN(11)));
                break;

            // Otherwise nothing to do...
            default:
                break;
        }
    }
}


/* By bit masking check which pins should be activated */
void activate_pin_on_specified_row(uint8_t display_values[columns], uint8_t row, uint8_t column) {
    unsigned mask = 1 << row;
    unsigned masked_number = display->display_values[column] & mask;
    unsigned pin_number = row_pins[row];
    if (masked_number > 0) {
        PTA->PDOR |= GPIO_PDOR_PDO(GPIO_PIN(pin_number));
    }
}

/* Disable all rows to remove unwanted shadow */
void disable_all_rows() {
    PTA->PDOR &= GPIO_PDOR_PDO(0xC0FFFD7F);
}

/* Maps char to our char_set and returns index */
uint8_t get_char_set_index(char c) {
    unsigned ascii_value = (unsigned) c;

    if (ascii_value >= ascii_A && ascii_value <= ascii_Z)
        // A - Z
        return ascii_value - ascii_A;

    else if (ascii_value >= ascii_a && ascii_value <= ascii_z)
        // a - z
        return ascii_value - ascii_a;

    else if (ascii_value == 32)
        // space
        return char_set_len_basic + 3;

    else if (ascii_value == 33)
        // !
        return char_set_len_basic + 1;

    else if (ascii_value == 46)
        // .
        return char_set_len_basic;

    else if (ascii_value == 63)
        // ?
        return char_set_len_basic + 2;

    else
        // unknown char
        return char_set_len_basic + 4;
}

/* Handle PIT interrupt -> renew column */
void PIT0_IRQHandler(void) {
    /* Iterate over columns to pretend, that all shining */
    /* Disable all rows to remove unwanted shadows */
    disable_all_rows();

    /* Activate column, and rows specified in display_values */
    column_select(display->column);

    for (uint8_t row = 0; row < rows; ++row) {
        activate_pin_on_specified_row(display->display_values, row, display->column);
    }

    /* Actualize for next column */
    if (display->column == columns - 1) {
        display->column = 0;
    } else {
        display->column++;
    }

    /* Timeout has occurred */
    PIT_TFLG0 |= PIT_TFLG_TIF_MASK;
}

/* Handle PIT interrupt -> update message context */
void PIT1_IRQHandler(void) {
    /* Update display_values and message index to know that we don't need to update more */
    actualize_values(message->message_length, message->char_set_indexes[display->message_index / char_width_with_space],
                     display->message_index);

    /* Actualize current message index */
    if (display->message_index == display->cycles_to_show_message - 1) {
        display->message_index = 0;
    } else {
        display->message_index++;
    }

    /* Timeout has occurred */
    PIT_TFLG1 |= PIT_TFLG_TIF_MASK;
}

/* Clear displayed values */
void clear_display() {
    for (unsigned i = 0; i < columns; i++) {
        display->display_values[i] = 0;
    }
    display->column = 0;
}

/* Set message context tu predefined structures */
void set_message(char *message_to_show) {
    /* Clear display and copy message */
    clear_display();
    strcpy(message->message_buffer, message_to_show);

    /* Calculate message len for next processing */
    uint8_t message_length;
    for (message_length = 0; message->message_buffer[message_length] != '\0'; ++message_length);
    message->message_length = message_length;
    /* Map chars to used char_set */
    unsigned long i;
    for (i = 0; i < message_length; ++i) {
        message->char_set_indexes[i] = get_char_set_index(message->message_buffer[i]);
    }

    /* Calculate how many iterations needed for showing current message and start iteration with index 0 */
    display->cycles_to_show_message = message->message_length * (char_width_with_space) + columns;
    display->message_index = 0;
}

int main(void) {
    /* Set up system */
    SystemConfig();

    /* Allocate display and clear all values */
    display = malloc(sizeof(display_struct));
    /* Allocate struct and set default message */
    message = malloc(sizeof(message_struct));

    /* Variables for message change */
    uint8_t pressed_left = 0, pressed_right = 0;
    uint8_t message_id = 0;

    set_message(messages[message_id]);

    /* Enable PIT interupts */
    enable_interrupts();

    /* Never leave main */
    for (;;) {
        if (!pressed_left && !(GPIOE_PDIR & BTN_SW5)) {
            if (message_id == 0) {
                message_id = messages_count - 1;
            } else {
                message_id--;
            }
            set_message(messages[message_id]);
            pressed_left = 1;
        } else if (GPIOE_PDIR & BTN_SW5) pressed_left = 0;
        // pressing the down button increases the compare value,
        // i.e. the compare event will occur less often;
        if (!pressed_right && !(GPIOE_PDIR & BTN_SW3)) {
            if (message_id == messages_count - 1) {
                message_id = 0;
            } else {
                message_id++;
            }
            set_message(messages[message_id]);
            pressed_right = 1;
        } else if (GPIOE_PDIR & BTN_SW3) pressed_right = 0;
    }
    return 0;
}

