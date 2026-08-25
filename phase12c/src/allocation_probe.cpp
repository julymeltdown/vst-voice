#include "seam/phase12c/live_voice.hpp"
#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
static std::atomic<unsigned long long>a{0};static thread_local bool p=false;void*operator new(std::size_t n){if(p)++a;if(void*x=std::malloc(n))return x;throw std::bad_alloc();}void operator delete(void*x)noexcept{std::free(x);}void operator delete(void*x,std::size_t)noexcept{std::free(x);}int main(){using namespace seam::phase12c;LiveVoiceEngine e;e.publishResource(makeEmbeddedHumanResource());constexpr unsigned N=64;std::array<float,N>l{},r{};float*o[2]={l.data(),r.data()};std::array<LiveEvent,1>on{{{0,EventType::NoteOn,1,0,60,.8f,{}}}};e.process(on,o,2,N);auto b=a.load();p=true;for(int i=0;i<100000;++i)e.process({},o,2,N);p=false;auto d=a.load()-b;std::cout<<"blocks=100000 allocations="<<d<<"\n";return d?1:0;}
