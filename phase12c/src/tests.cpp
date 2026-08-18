#include "seam/phase12c/live_voice.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>
static std::atomic<unsigned long long> g_alloc{0};static thread_local bool g_probe=false;
void*operator new(std::size_t n){if(g_probe)++g_alloc;if(void*p=std::malloc(n))return p;throw std::bad_alloc();}void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}
int main(){using namespace seam::phase12c;auto r=makeEmbeddedHumanResource();assert(r&&r->valid()&&r->bytes()<kMaxResourceBytes);ResourcePublisher pub;assert(pub.publish(r));assert(pub.acquireForAudio()==r.get());pub.releaseFromAudio();LiveVoiceEngine e;e.configure(48000,2);assert(e.publishResource(r));constexpr unsigned N=512;std::array<float,N>l{},rr{};float*o[2]={l.data(),rr.data()};std::array<LiveEvent,4>ev{{{64,EventType::NoteOn,1,0,60,.9f,{}},{192,EventType::Pressure,1,0,60,.4f,{}},{256,EventType::PitchBend,1,0,60,2.f,{}},{400,EventType::NoteOff,1,0,60,0,{}}}};g_probe=true;auto before=g_alloc.load();e.process(ev,o,2,N);g_probe=false;assert(g_alloc.load()==before);for(unsigned i=0;i<64;++i)assert(l[i]==0);double energy=0;for(float x:l){assert(std::isfinite(x));energy+=std::abs(x);}assert(energy>1);std::vector<LiveEvent>many;many.reserve(33);for(int i=0;i<33;++i)many.push_back({0,EventType::NoteOn,i,0,std::int16_t(48+i%24),.7f,{}});e.process(many,o,2,N);assert(e.stats().steals>=1);std::array<LiveEvent,2>midi{{{0,EventType::Midi1,-1,0,0,0,{0x90,64,100}},{300,EventType::Midi1,-1,0,0,0,{0x80,64,0}}}};e.process(midi,o,2,N);assert(e.stats().midiEvents>=2);auto bad=std::make_shared<LiveVoicebankResource>(*r);bad->trusted=false;assert(!e.publishResource(bad));std::cout<<"PHASE12C_LIVE_TESTS=PASS energy="<<energy<<" steals="<<e.stats().steals<<"\n";}
