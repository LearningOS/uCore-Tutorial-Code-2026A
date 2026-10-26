#ifndef VIRTIO_INPUT_H
#define VIRTIO_INPUT_H
#include "types.h"
#define INPUT_KEYBOARD 0
#define INPUT_MOUSE 1
void virtio_input_init(void);
void virtio_input_handle_irq(int device);
int virtio_input_is_empty(int device);
uint64 virtio_input_read_event(int device);
#endif
