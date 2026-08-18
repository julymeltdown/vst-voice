#include "seam/phase12c/live_voice.hpp"
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
int main(int argc,char**argv){using namespace seam::phase12c;std::string out=argc>1?argv[1]:"phase12c-matrix.json";const int rates[]={44100,48000,88200,96000,176400,192000};const int bufs[]={16,32,64,128,256,512,1024};const int chans[]={1,2,4,8};unsigned cases=0,fail=0;double total=0;for(int sr:rates)for(int n:bufs)for(int ch:chans)for(int mode=0;mode<2;++mode){LiveVoiceEngine e;e.configure(sr,ch);std::vector<std::vector<float>>mem(ch,std::vector<float>(n));std::array<float*,8>ptr{};for(int c=0;c<ch;++c)ptr[c]=mem[c].data();std::array<LiveEvent,2>ev{{{0,EventType::NoteOn,1,0,60,.8f,{}},{std::uint32_t(n/2),EventType::PitchBend,1,0,60,mode?7.f:2.f,{}}}};e.process(ev,ptr.data(),ch,n);bool ok=true;for(auto&v:mem)for(float x:v){ok&=std::isfinite(x);total+=std::abs(x);}if(!ok)++fail;++cases;}std::ofstream f(out);f<<"{\n  \"cases\": "<<cases<<",\n  \"expected\": 336,\n  \"failures\": "<<fail<<",\n  \"finite\": "<<(fail==0?"true":"false")<<",\n  \"result\": \""<<(cases==336&&fail==0?"PASS":"FAIL")<<"\"\n}\n";std::cout<<"cases="<<cases<<" fail="<<fail<<" energy="<<total<<"\n";return cases==336&&fail==0?0:1;}
