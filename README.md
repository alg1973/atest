 Since liblame don't have public API for WAVE files but only have static internal routines for lame frontend utilites.  I decided to write some classes  based on lame static routines and WAVE specification. For production project should be used some public library with Wave support. But since I'm not familiar with Windows development and Windows build environment only including code for Wave files in project sources may guarantee stable building on Windows or other remote platform for me. 
 I implemented  support only for  Wave files with PCM (type==1) format and interger samples encoding. I have included three WAV files with PCM with 16 bits samples. I setup output mp3 only with quality settings (4 is reasonably good) and let libLAME choose compression ratio themself. If desired other setup parameters may be easily installed with lame_set_* functions (i.e. lib_set_brate for compression)
I have put all source code in one "cc" file only for reading convenience. For real project interfaces should go to header files.I wrote code with minimum generalization. For real projects classes may be extended with little effort.  
Build & compile with: g++ -std=c++11 -o wav2mp3 encoder.cc -pthread -static -lmp3lame 
I have installed libmp3lame in standard places. If LAME lie in unusual places please add -I/path/to/lame/include -L/path/to/lame/libraries 
I use only std::shared_ptr from c++11 other then that all should compile with c++98. One can replace shared_ptr<string> with string with cost of filename copying. I use std::string  with only light cost of copying. One may use standard C null terminated string in order to avoid copying. It may be reasonable in one place while forwarding d_name filename field in dirent structure to thread pool. But I decided not to do it in favor of code readability and simplicity.    
For utilizing all available cores I use thread pool with number of threads equal to available CPUs/cores. I check for WAV file only with filename extention. Only ".wav" case insenitive. One can read all regular files and check for "RIFF" in first bytes and "WAVE" at offset=8.
For Windows building I use Cygwin g++ package.

