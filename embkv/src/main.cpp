#include <ev.h>
#include <iostream>
#include <csignal>

static void sigint_cb(EV_P_ ev_signal *w, int revents) {
    std::cout << "Ctrl+C pressed!\n";
    ev_break(loop, EVBREAK_ALL);
}

int main() {
    struct ev_loop *loop = EV_DEFAULT;
    ev_signal signal_watcher;

    // 监听 SIGINT 信号 （Ctrl+C）
    ev_signal_init(&signal_watcher, sigint_cb, SIGINT);
    ev_signal_start(loop, &signal_watcher);

    ev_run(loop, 0);
    return 0;
}