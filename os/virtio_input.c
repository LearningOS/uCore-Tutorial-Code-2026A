/* Provided keyboard/mouse support; mirrors rCore ch9 InputDevice semantics.
 * Both devices deliver Linux input_event type/code/value through VirtIO.
 * This support code is not part of the eight student exercises. */
#include "defs.h"
#include "io.h"
#include "virtio_input.h"
#include "virtio_mmio.h"

#define INPUT_EVENTS 256
struct input_event { uint16 type, code; uint32 value; };
struct input_device {
	struct vmmio_device device;
	struct virtqueue events_queue, status_queue;
	struct input_event dma_events[VQ_NUM];
	int token_buffer[VQ_NUM];
	uint64 events[INPUT_EVENTS];
	uint32 head, count;
	struct io_cond condvar;
};
static struct input_device input_devices[2];

static struct input_device *input_device(int device)
{
	if (device != INPUT_KEYBOARD && device != INPUT_MOUSE)
		panic("invalid input device");
	return &input_devices[device];
}

void virtio_input_init(void)
{
	irq_state_t state = io_irq_save();
	for (int i = 0; i < 2; ++i) {
		struct input_device *device = &input_devices[i];
		uint64 base = i == INPUT_KEYBOARD ? 0x10005000UL : 0x10006000UL;
		vmmio_init(&device->device, base, 18);
		vq_init(&device->device, &device->events_queue, 0);
		vq_init(&device->device, &device->status_queue, 1);
		io_cond_init(&device->condvar);
		vmmio_ready(&device->device);
		for (int event = 0; event < VQ_NUM; ++event) {
			struct vq_buf buffer = {&device->dma_events[event],
					       sizeof(struct input_event), 1};
			int token = vq_submit(&device->events_queue, &buffer, 1);
			if (token < 0)
				panic("cannot post input buffer");
			device->token_buffer[token] = event;
		}
	}
	io_irq_restore(state);
}

int virtio_input_is_empty(int which)
{
	irq_state_t state = io_irq_save();
	int empty = input_device(which)->count == 0;
	io_irq_restore(state);
	return empty;
}

uint64 virtio_input_read_event(int which)
{
	struct input_device *device = input_device(which);
	for (;;) {
		irq_state_t state = io_irq_save();
		if (device->count) {
			uint64 event = device->events[device->head];
			device->head = (device->head + 1) % INPUT_EVENTS;
			device->count--;
			io_irq_restore(state);
			return event;
		}
		io_wait_no_sched(&device->condvar);
		io_irq_restore(state);
		sched();
	}
}

void virtio_input_handle_irq(int which)
{
	struct input_device *device = input_device(which);
	irq_state_t state = io_irq_save();
	vmmio_ack(&device->device);
	int received = 0, token;
	while ((token = vq_pop_used(&device->events_queue)) >= 0) {
		int index = device->token_buffer[token];
		struct input_event event = device->dma_events[index];
		if (device->count == INPUT_EVENTS)
			panic("input event buffer full");
		device->events[(device->head + device->count) % INPUT_EVENTS] =
			((uint64)event.type << 48) | ((uint64)event.code << 32) |
			event.value;
		device->count++;
		received++;
		struct vq_buf buffer = {&device->dma_events[index],
				       sizeof(struct input_event), 1};
		int next_token = vq_submit(&device->events_queue, &buffer, 1);
		if (next_token < 0)
			panic("cannot repost input buffer");
		device->token_buffer[next_token] = index;
	}
	io_irq_restore(state);
	if (received)
		io_signal(&device->condvar);
}
