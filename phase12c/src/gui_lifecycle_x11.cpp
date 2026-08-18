#if defined(__linux__)
#include <X11/Xlib.h>
#include <iostream>
int main(){Display*d=XOpenDisplay(nullptr);if(!d){std::cerr<<"no DISPLAY\n";return 2;}for(int i=0;i<1000;++i){Window w=XCreateSimpleWindow(d,DefaultRootWindow(d),0,0,64,64,0,0,0);XMapWindow(d,w);XSync(d,False);XUnmapWindow(d,w);XDestroyWindow(d,w);}XSync(d,False);XCloseDisplay(d);std::cout<<"guiCycles=1000 PASS\n";return 0;}
#else
int main(){return 77;}
#endif
