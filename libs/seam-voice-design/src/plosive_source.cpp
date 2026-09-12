#include "seam/voice_design/plosive_source.hpp"
#include <algorithm>
#include <cmath>

namespace seam::voice_design {
core::Result<PlosiveSource> PlosiveSource::create(PlosiveConfig config,std::uint32_t sampleRate,time::SampleFrame origin) {
  const auto frames=static_cast<std::uint64_t>(config.closureFrames)+config.burstFrames;
  if (sampleRate<8000U || sampleRate>384000U || config.closureFrames==0U || config.burstFrames<3U ||
      frames>static_cast<std::uint64_t>(sampleRate)*2U || origin<0 || origin>(time::SampleFrame{1}<<52)-static_cast<time::SampleFrame>(frames))
    return core::failure<PlosiveSource>(core::ErrorCode::InvalidArgument,"Plosive closure, burst or timeline exceeds bounds");
  auto burst=FricationSource::create(config.burst,sampleRate,origin+config.closureFrames);
  if (!burst) return core::Result<PlosiveSource>{burst.error()};
  return PlosiveSource{config,sampleRate,origin,std::move(burst.value())};
}

core::Result<synthesis::PhraseAudio> PlosiveSource::render(std::size_t frames,std::stop_token stop) {
  using Output=synthesis::PhraseAudio;
  if (frames==0U || frames>static_cast<std::size_t>(end()-position_))
    return core::failure<Output>(core::ErrorCode::InvalidArgument,"Plosive block exceeds its finite gesture");
  const auto cancelled=[] { return core::failure<Output>(core::ErrorCode::Conflict,"Plosive rendering cancelled"); };
  if (stop.stop_requested()) return cancelled();
  auto next=*this;
  Output output{position_,std::vector<float>(frames,0.0F)};
  const auto finish=position_+static_cast<time::SampleFrame>(frames);
  const auto start=std::max(position_,burstStart());
  if (start<finish) {
    const auto audio=next.burst_.render(static_cast<std::size_t>(finish-start),stop);
    if (!audio) return core::Result<Output>{audio.error()};
    if (audio.value().startFrame!=start)
      return core::failure<Output>(core::ErrorCode::InvariantViolation,"Plosive burst clock differs from its gesture");
    const auto ramp=std::max<std::uint32_t>(1U,std::min(sampleRate_/1000U,config_.burstFrames/4U));
    const auto smooth=[](double value) { return value*value*(3.0-2.0*value); };
    for (std::size_t i=0U;i<audio.value().samples.size();++i) {
      if (i%256U==0U && stop.stop_requested()) return cancelled();
      const auto frame=start+static_cast<time::SampleFrame>(i);
      const auto local=frame-burstStart(), remaining=end()-1-frame;
      const auto attack=smooth(std::min(1.0,static_cast<double>(local)/ramp));
      const auto release=smooth(std::min(1.0,static_cast<double>(remaining)/ramp));
      const auto decay=std::exp(-6.0*static_cast<double>(std::max<time::SampleFrame>(0,local-ramp))/
          static_cast<double>(config_.burstFrames-ramp-1U));
      output.samples[static_cast<std::size_t>(frame-position_)]=static_cast<float>(audio.value().samples[i]*attack*release*decay);
    }
  }
  if (stop.stop_requested()) return cancelled();
  next.position_=finish; *this=std::move(next);
  return output;
}
}
