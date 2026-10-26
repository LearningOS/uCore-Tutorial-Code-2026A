#include "user.h"

static unsigned char *framebuffer;

static void pixel(int x, int y, unsigned char red, unsigned char green, unsigned char blue)
{
    if (x < 0 || x >= VIRTGPU_XRES || y < 0 || y >= VIRTGPU_YRES) return;
    usize i = ((usize)y * VIRTGPU_XRES + (usize)x) * 4;
    framebuffer[i] = blue;
    framebuffer[i + 1] = green;
    framebuffer[i + 2] = red;
}

/* Integer line-segment distance gives a ten-pixel outline without floating point. */
static int on_segment(int x, int y, int ax, int ay, int bx, int by)
{
    long dx = bx - ax, dy = by - ay;
    long px = x - ax, py = y - ay;
    long dot = px * dx + py * dy;
    long length = dx * dx + dy * dy;
    if (dot < 0) return px * px + py * py <= 25;
    if (dot > length) {
        px = x - bx; py = y - by;
        return px * px + py * py <= 25;
    }
    long cross = px * dy - py * dx;
    return cross * cross <= 25 * length;
}

static void paint(int center_x)
{
    const int center_y = 400;
    for (int y = center_y - 80; y <= center_y + 80; ++y)
        for (int x = center_x - 80; x <= center_x + 80; ++x) {
            int dx = x - center_x, dy = y - center_y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx <= 79 && dy <= 79 && (dx >= 70 || dy >= 70))
                pixel(x, y, 255, 0, 0);
        }
    /* Circle top-left=(center_x-70,100), diameter150; solid blue. */
    for (int y = 100; y < 250; ++y)
        for (int x = center_x - 70; x < center_x + 80; ++x) {
            int dx = 2 * (x - (center_x - 70)) - 149;
            int dy = 2 * (y - 100) - 149;
            if (dx * dx + dy * dy < 150 * 150) pixel(x, y, 0, 0, 255);
        }
    /* The same three vertices and green outline as the chapter's example. */
    for (int y = 545; y <= 705; ++y)
        for (int x = center_x - 125; x <= center_x + 85; ++x)
            if (on_segment(x, y, center_x, 550, center_x + 80, 600) ||
                on_segment(x, y, center_x + 80, 600, center_x - 120, 700) ||
                on_segment(x, y, center_x - 120, 700, center_x, 550))
                pixel(x, y, 0, 255, 0);
}

int main(void)
{
    framebuffer = u_framebuffer();
    CHECK((usize)framebuffer == FRAMEBUFFER_ADDRESS, "framebuffer mapping failed");
    memset(framebuffer, 0, VIRTGPU_LEN);
    int x = 80;
    for (int i = 0; i < 5; ++i) { x += 150 + 20; paint(x); }
    CHECK(u_framebuffer_flush() == 0, "framebuffer flush failed");
    return 0;
}
