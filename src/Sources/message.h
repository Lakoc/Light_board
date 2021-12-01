#ifndef MESSAGE_H
#define MESSAGE_H
/* Constants for better working with ascii values */
#define ascii_A 65
#define ascii_Z 90
#define ascii_a 97
#define ascii_z 122

#define max_message_len 200
#define messages_count 6

/* Predefined message */
char messages[messages_count][max_message_len] = {"HELLO WORLD", "FIT VUT", "IMP", "Alexander Polok",
                                                  "UNKNOWN #$(&^^@", "SPACES    "};

typedef struct message {
    char message_buffer[max_message_len];
    uint8_t message_length;
    uint8_t message_index;
    uint8_t char_set_indexes[max_message_len];
} message_struct;

#endif
