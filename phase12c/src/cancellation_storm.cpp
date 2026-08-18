#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>
class Latest{public:Latest():w([this](std::stop_token t){run(t);}){}~Latest(){w.request_stop();cv.notify_all();}void submit(std::uint64_t r){{std::lock_guard l(m);pending=r;has=true;}cv.notify_all();}std::uint64_t done()const{return published.load();}private:void run(std::stop_token t){while(!t.stop_requested()){std::uint64_t r;{std::unique_lock l(m);cv.wait(l,t,[this]{return has;});if(t.stop_requested())return;r=pending;has=false;}if(r==pending)published.store(r);}}std::mutex m;std::condition_variable_any cv;bool has=false;std::uint64_t pending=0;std::atomic<std::uint64_t>published{0};std::jthread w;};int main(){Latest x;for(std::uint64_t i=1;i<=10000;++i)x.submit(i);for(int i=0;i<200&&x.done()!=10000;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));std::cout<<"submitted=10000 published="<<x.done()<<"\n";return x.done()==10000?0:1;}
