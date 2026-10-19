#include "audio_route.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

namespace nbt {

namespace {

struct RouteJob {
    std::string command;
};

void* routeWorker(void* arg) {
    RouteJob* job = static_cast<RouteJob*>(arg);
    printf("[AudioRoute] exec: %s\n", job->command.c_str());

    FILE* pipe = popen(job->command.c_str(), "r");
    if (!pipe) {
        printf("[AudioRoute] popen failed\n");
    } else {
        char line[256];
        int lines = 0;
        while (fgets(line, sizeof(line), pipe) && lines < 20) {
            size_t n = strlen(line);
            while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
            if (n > 0) {
                printf("[AudioRoute]   | %s\n", line);
                ++lines;
            }
        }
        int status = pclose(pipe);
        printf("[AudioRoute] exit status %d\n", status);
    }
    delete job;
    return NULL;
}

} // namespace

AudioRoute::AudioRoute() {}

bool AudioRoute::configure() {
    path_.clear();
    suffix_.clear();

    const char* env_suffix = getenv("NBT_AUDIOCONN_SUFFIX");
    if (env_suffix) suffix_ = env_suffix;

    const char* candidates[3] = {
        getenv("NBT_AUDIOCONN"),
        "/fs/sda0/opt/mm/bin/audioconn",
        "/opt/mm/bin/audioconn"
    };
    for (int i = 0; i < 3; ++i) {
        if (candidates[i] && candidates[i][0] && access(candidates[i], X_OK) == 0) {
            path_ = candidates[i];
            break;
        }
    }
    if (path_.empty()) {
        printf("[AudioRoute] audioconn not found; automatic routing disabled.\n");
        return false;
    }
    printf("[AudioRoute] using %s\n", path_.c_str());
    return true;
}

bool AudioRoute::run(char op, uint32_t a, uint32_t b, bool two_args) {
    if (path_.empty() || a == 0) return false;

    char arg[96];
    if (two_args) {
        snprintf(arg, sizeof(arg), " -%c 0x%x 0x%x", op, static_cast<unsigned>(a), static_cast<unsigned>(b));
    } else {
        snprintf(arg, sizeof(arg), " -%c 0x%x", op, static_cast<unsigned>(a));
    }

    RouteJob* job = new RouteJob();
    job->command = path_ + arg + suffix_ + " 2>&1";

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t tid;
    int rc = pthread_create(&tid, &attr, &routeWorker, job);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        delete job;
        return false;
    }
    return true;
}

bool AudioRoute::build(uint32_t conn_id)  { return run('b', conn_id, 0, false); }
bool AudioRoute::remove(uint32_t conn_id) { return run('r', conn_id, 0, false); }
bool AudioRoute::buildFocus(uint32_t src, uint32_t sink)  { return run('b', src, sink, true); }
bool AudioRoute::removeFocus(uint32_t src, uint32_t sink) { return run('r', src, sink, true); }

} // namespace nbt
