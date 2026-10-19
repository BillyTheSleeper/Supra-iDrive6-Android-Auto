#ifndef AUDIO_ROUTE_HPP
#define AUDIO_ROUTE_HPP

// Stop-gap source->speaker routing through the stock `audioconn` CLI.
//
// audioconn is the test client of the head unit's ConnectionMaster. Stock
// CarPlay audio is routed by the HMI building a ConnectionMaster connection for
// an AuxInCarPlay source; the corresponding DSI service has not been
// reverse-engineered yet, so for now we shell out to:
//
//     audioconn -b 0x<connId>     (build)       audioconn -r 0x<connId>   (remove)
//
// Two forms exist (from `audioconn` usage):
//     -b|-r <connId>        direct ConnectionMaster build/remove. Observed to
//                           FAIL ("CMCommand error in execution") for CarPlay
//                           connections on the car.
//     -b|-r <src> <snk>     requestFocus/releaseFocus over the AudioManagement
//                           DSI interface (no SourceActivity sent). This is the
//                           HMI-style path and the default here.
// <src>/<snk> are the eSRC/eSNK codes shown in `audioconn -l`:
//     0xb9 eSRC_A4A_CARPLAY_ENT_MEDIA   0xba eSRC_A4A_CARPLAY_INT_MIX
//     0xb0 eSRC_CARPLAY_ENT             0xb3 eSRC_CARPLAY_NAV     sink 0x1 = SPEAKERS
//
// Commands run on a detached worker thread so they can never stall the
// protocol thread. Environment overrides:
//     NBT_AUDIOCONN         path to audioconn
//     NBT_AUDIOCONN_SUFFIX  extra arguments appended to -b/-r (e.g. " 0x01")

#include <stdint.h>
#include <string>

namespace nbt {

class AudioRoute {
public:
    AudioRoute();

    // Resolves the audioconn binary (env override, then known locations).
    bool configure();
    bool available() const { return !path_.empty(); }

    // Fire-and-forget; return false only if the job could not be queued.
    bool build(uint32_t conn_id);
    bool remove(uint32_t conn_id);
    bool buildFocus(uint32_t src, uint32_t sink);
    bool removeFocus(uint32_t src, uint32_t sink);

private:
    bool run(char op, uint32_t a, uint32_t b, bool two_args);

    std::string path_;
    std::string suffix_;
};

} // namespace nbt

#endif // AUDIO_ROUTE_HPP
