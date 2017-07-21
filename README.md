# atest
# Since liblame don't have public API for WAVE files only internal routines for lame frontend utilites  I decide to write some classes  based on lame static routines and WAVE specification.  Offcourse for production project should be used some public library for Wave support. But since I'm not familiar with Windows development and Windows build environment only including code for Wave files in project sources may garantee stable building on Windows or other remote platform. 
# I included only  Wave files with PCM (type==1) format. 

build & compile with g++ -std=c++11 -o wav2mp3 encoder.cc -pthread -static -lmp3lame
