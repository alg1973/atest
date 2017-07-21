 Since liblame don't have public API for WAVE files but only have static internal routines for lame frontend utilites  I decided to write some classes  based on lame static routines and WAVE specification. For production project should be used some public library with Wave support. But since I'm not familiar with Windows development and Windows build environment only including code for Wave files in project sources may guarantee stable building on Windows or other remote platform. 
 I included only  Wave files with PCM (type==1) format. 

I have put all source code in one "cc" file only for reading convenience. For real project interfaces should go to header files.I wrote code with minimum generalization. For real projects classes may be extended with little effort.  

build & compile with: g++ -std=c++11 -o wav2mp3 encoder.cc -pthread -static -lmp3lame 
I have installed libmp3lame in standard places. If LAME lie in unusual places please add -I/path/to/lame/include -L/path/to/lame/libraries 
I use only std::shared_ptr from c++11 other then that all should compile with c++98. One can replace shared_ptr<string> with string with cost of filename copiyng.
For utilizing all available cores I use thread pool with number of threads equal to available CPUs.


