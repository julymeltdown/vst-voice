#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/clap/session.hpp"
#include "seam/clap/state_codec.hpp"
#include "seam/core/sha256.hpp"

#include <cmath>
#include <limits>

TEST_CASE("CLAP state codec preserves bounded multichannel PCM") {
  auto session = seam::clap::makeDiagnosticSession(48000U, 4U, 0.05);
  CHECK(session);
  session.value().masterGainDb = -3.5;
  session.value().title = "가창 렌더 / 歌声 render";
  const auto encoded = seam::clap::encodeState(session.value());
  CHECK(encoded);
  const auto decoded = seam::clap::decodeState(encoded.value());
  CHECK(decoded);
  CHECK(decoded.value() == session.value());
}

TEST_CASE("CLAP state codec rejects corruption and non-finite PCM") {
  auto session = seam::clap::makeDiagnosticSession(44100U, 2U, 0.02);
  CHECK(session);
  auto encoded = seam::clap::encodeState(session.value());
  CHECK(encoded);
  encoded.value()[encoded.value().size() / 2U] ^= std::byte{0x40};
  CHECK(!seam::clap::decodeState(encoded.value()));
  session.value().interleavedSamples[0] = std::numeric_limits<float>::quiet_NaN();
  CHECK(!seam::clap::encodeState(session.value()));
}

TEST_CASE("CLAP state file uses durable round trip") {
  const auto directory = seam::test::support::temporaryDirectory("clap-state");
  auto session = seam::clap::makeDiagnosticSession(48000U, 1U, 0.03);
  CHECK(session);
  const auto path = directory / "session.seamclapstate";
  CHECK(seam::clap::writeStateFile(path, session.value()));
  const auto loaded = seam::clap::readStateFile(path);
  CHECK(loaded);
  CHECK(loaded.value() == session.value());
}

TEST_CASE("CLAP session resampling keeps channels and duration") {
  auto source = seam::clap::makeDiagnosticSession(48000U, 3U, 0.1);
  CHECK(source);
  const auto output = seam::clap::resampleSession(source.value(), 44100U);
  CHECK(output);
  CHECK(output.value().sampleRate == 44100U);
  CHECK(output.value().channelCount == 3U);
  CHECK_NEAR(static_cast<double>(output.value().frameCount()), 4410.0, 1.0);
  CHECK(output.value().validate());
}

TEST_CASE("CLAP master gain conversion follows decibel scale") {
  CHECK_NEAR(seam::clap::gainFromDecibels(0.0), 1.0, 1.0e-6);
  CHECK_NEAR(seam::clap::gainFromDecibels(-6.0), 0.501187, 1.0e-5);
  CHECK(seam::clap::gainFromDecibels(-100.0) == 0.0F);
}

TEST_CASE("CLAP master gain parser accepts exact classic-locale decimals") {
  const auto zero = seam::clap::parseMasterGainDb("0.0");
  CHECK(zero);
  CHECK_NEAR(zero.value(), 0.0, 1.0e-12);
  const auto exponent = seam::clap::parseMasterGainDb("-6.25e0");
  CHECK(exponent);
  CHECK_NEAR(exponent.value(), -6.25, 1.0e-12);
}

TEST_CASE("CLAP master gain parser rejects malformed or unsafe values") {
  for (const auto value : {"1.0suffix", " nan", "nan", "inf", "1e999",
                           "-60.1", "6.1", ""}) {
    CHECK(!seam::clap::parseMasterGainDb(value));
  }
}

TEST_CASE("CLAP state rejects overflowed declared frame counts before allocation") {
  auto session = seam::clap::makeDiagnosticSession(48000U, 2U, 0.01);
  CHECK(session);
  auto encoded = seam::clap::encodeState(session.value());
  CHECK(encoded);
  for (std::size_t index = 24U; index < 32U; ++index)
    encoded.value()[index] = std::byte{0xFF};
  seam::core::Sha256 hash;
  hash.update(std::span<const std::byte>{encoded.value()}.first(encoded.value().size() - 32U));
  const auto digest = hash.digest();
  std::copy(digest.begin(), digest.end(), encoded.value().end() - 32);
  CHECK(!seam::clap::decodeState(encoded.value()));
}
