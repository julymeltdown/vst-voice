#include "seam/phase12c/live_voice.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
int main(int argc,char**argv){using namespace seam::phase12c;std::string profile="smoke",out="phase12c-soak.json";for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--profile"&&i+1<argc)profile=argv[++i];else if(a=="--output"&&i+1<argc)out=argv[++i];}const auto required=profile=="full"?7200:5;LiveVoiceEngine e;std::array<float,64>l{},r{};float*o[2]={l.data(),r.data()};std::array<LiveEvent,1>on{{{0,EventType::NoteOn,1,0,60,.8f,{}}}};e.process(on,o,2,64);auto start=std::chrono::steady_clock::now();std::uint64_t blocks=0;bool finite=true;while(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-start).count()<required){e.process({},o,2,64);for(float x:l)finite&=std::isfinite(x);++blocks;std::this_thread::sleep_for(std::chrono::microseconds(100));}auto elapsed=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-start).count();bool pass=finite&&elapsed>=required;std::ofstream f(out);f<<"{\n \"profile\": \""<<profile<<"\",\n \"requiredSeconds\": "<<required<<",\n \"elapsedSeconds\": "<<elapsed<<",\n \"blocks\": "<<blocks<<",\n \"result\": \""<<(pass?"PASS":"FAIL")<<"\"\n}\n";return pass?0:1;}
