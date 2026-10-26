// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
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
    int retries;        // extra attempts after a non-zero exit (build requests only)
    int delayMs;        // wait before running (connection snapshots)
    int maxLines;       // output lines copied into the log
    RouteJob() : retries(0), delayMs(0), maxLines(20) {}
};

// NBT_AUDIOCONN_RETRIES (default 3, max 10): a build that fails right after the phone connects
// (e.g. "AMCommand error" while the car's BT audio source is still settling) is retried every 3 s.
int buildRetries() {
    const char* e = getenv("NBT_AUDIOCONN_RETRIES");
    if (!e || !*e) return 3;
    int n = atoi(e);
    if (n < 0) n = 0;
    if (n > 10) n = 10;
    return n;
}

int runOnce(const std::string& command, int maxLines = 20) {
    printf("[AudioRoute] exec: %s\n", command.c_str());
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        printf("[AudioRoute] popen failed\n");
        return -1;
    }
    char line[256];
    int lines = 0;
    while (fgets(line, sizeof(line), pipe)) {
        if (lines >= maxLines) continue;   // drain the pipe, log only the first maxLines
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (n > 0) {
            printf("[AudioRoute]   | %s\n", line);
            ++lines;
        }
    }
    int status = pclose(pipe);
    printf("[AudioRoute] exit status %d\n", status);
    return status;
}

void* routeWorker(void* arg) {
    RouteJob* job = static_cast<RouteJob*>(arg);
    if (job->delayMs > 0) usleep((useconds_t)job->delayMs * 1000);
    int status = runOnce(job->command, job->maxLines);
    for (int attempt = 1; status != 0 && attempt <= job->retries; ++attempt) {
        printf("[AudioRoute] request failed: retry %d/%d in 3 s\n", attempt, job->retries);
        sleep(3);
        status = runOnce(job->command);
    }
    if (status != 0 && job->retries > 0)
        printf("[AudioRoute] request still failing after %d retries: select Bluetooth audio in iDrive manually\n",
               job->retries);
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
    job->retries = (op == 'b') ? buildRetries() : 0;
    return spawn(job);
}

// Read-only snapshot of the car's active audio connections (`audioconn -l`), e.g. while the Assistant
// replies, to see whether the Bluetooth source is ducked or another (voice) connection is active.
bool AudioRoute::listConnections(const char* tag, int delayMs) {
    if (path_.empty()) return false;
    printf("[AudioRoute] connection snapshot in %d ms (%s)\n", delayMs, tag ? tag : "");
    RouteJob* job = new RouteJob();
    job->command = path_ + " -l" + suffix_ + " 2>&1";
    job->delayMs = delayMs;
    job->maxLines = 40;
    return spawn(job);
}

bool AudioRoute::spawn(void* p) {
    RouteJob* job = static_cast<RouteJob*>(p);
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
