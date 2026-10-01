#pragma once
#include "klattalker/sequencer.hpp"
#include <memory>
#include <span>
#include <vector>

namespace klattalker {

enum class EngineKind { Klsyn80, Klsyn88 };

// forward decl — defined in synth.hpp
struct VoiceQuality;

// Common engine interface: take per-sample control tracks + an F0 track,
// produce a float audio buffer in [-1, 1]. Both klsyn80 and klsyn88
// honour the same ControlTracks layout.
class Engine {
public:
    virtual ~Engine() = default;
    virtual std::vector<float> render(const ControlTracks& tracks,
                                      std::span<const double> f0_track) = 0;
};

std::unique_ptr<Engine> make_engine(EngineKind kind, int sample_rate,
                                    const VoiceQuality& vq);

}  // namespace klattalker
