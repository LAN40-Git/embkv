#include "log.h"
#include <ev.h>

static void accept_cb(struct ev_loop* loop, ev_io *w, int revents) {
    if (revents & EV_READ) {

    }
}

int main() {
    struct ev_loop* loop = EV_DEFAULT;

    ev_io accept_watcher;
    // ev_io_init(&accept_watcher, accept_cb, );
}