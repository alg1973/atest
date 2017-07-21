#include <cctype>
#include <string>
#include <iostream>
#include <list>
#include <fstream>
#include <vector>
#include <memory>
#include <exception>
#include <lame/lame.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <pthread.h>



using namespace std;



//
// Class for Wav file reading. It only tested with PCM files with 16 bit samples.
//

class Wave_Reader {
public:
	Wave_Reader(const string& wname): wave_file(wname.c_str(),std::ios_base::binary | std::ios_base::in ) {
		if(!wave_file)
        		throw runtime_error("Unable to open file");
		wave_file.exceptions ( std::ifstream::failbit | std::ifstream::badbit );
		read_header();
		if (format!=1) 
        		throw runtime_error("Only PCM Wave file supported");
		
	} 
	virtual ~Wave_Reader() {
		wave_file.close();
	}
	void read_header(void); 

	// Support routines for reading binary file.
	
	// Skip "s" bytes on input Wav file
	void skip_bytes(int s) {
		wave_file.seekg(s,ios_base::cur);
	}
	// Read 32 bit integer in big-endian mode
	int read32_hilo(void) {
		char buf[4];
		wave_file.read(&buf[0],4);
		char rbuf[4] = { buf[3],buf[2],buf[1],buf[0]};
		return *(reinterpret_cast<int*>(&rbuf[0]));
	}

	// Read 32 bit integer in little-endian mode
	int read32_lohi(void) {
		int32_t data;
		wave_file.read(reinterpret_cast<char*>(&data),4);
		return data;
	}

	// Read 16 integer in little-endian mode
	int read16_lohi(void) {
		int16_t data;
		wave_file.read(reinterpret_cast<char*>(&data),2);
		return data;
	}
	// Read Wav sample data into "d" vector.
	vector<char>& read(vector<char>& d) {
		d.resize(data_size);
		wave_file.read(&d[0],data_size);
		data_size=0;
		return d;
	}


	//
	// Wave file header data entries getters functions
	//


	// Wav types. Only 1 - PCM supported here.
	int get_format(void) {
		return format;
	}

	// Number of channels: 1 - mono, 2 - Stereo.
	int get_channels(void) {
		return channels;
	}
	
	//Sample size.
	int get_bits_per_sample(void) {
		return bits_per_sample;
	}

	// Wav Sample rate.
	int get_samples_per_sec(void) {
		return samples_per_sec;
	}

	int get_avg_bytes_per_sec(void) {
		return avg_bytes_per_sec;
	}
	// Sample data size.
	int get_size(void) {
		return data_size;
	}

private:

	static const int ID_RIFF = 0x52494646; /* "RIFF" */
	static const int ID_WAVE = 0x57415645; /* "WAVE" */
	static const int ID_FMT = 0x666d7420; /* "fmt " */
	static const int ID_DATA = 0x64617461; /* "data" */

	int     channels;
	int 	format;
    	int     block_align;
    	int     bits_per_sample;
    	int     samples_per_sec;
    	int     avg_bytes_per_sec;
	int 	data_size;
	ifstream wave_file;
};

//
// Simple wrapper around LAME mp3 encoder.
//

class Encoder {
public:
		Encoder(Wave_Reader& wr): wave(wr) {
			  if (!(gfp = lame_init()))
				throw runtime_error("Unable to init LAME library");
			 
			  // Set lame parametres based on Wav file data. 
			  lame_set_num_channels(gfp,wave.get_channels());
   			  lame_set_in_samplerate(gfp,wave.get_samples_per_sec());
			  /* 4 is for good quality */
   			  lame_set_quality(gfp,4);  

			  // LAME choose other parameters  for us automatically (stereo type, compression rate, etc);
			  if(lame_init_params(gfp)<0)
				throw runtime_error("Unable to setup LAME library");
		}

		virtual ~Encoder() {
			lame_close(gfp);
		}
	
		void encode (const string& mp3n);
private:	
 lame_global_flags *gfp;
 Wave_Reader& wave;
};


//
// Multithreaded queue with threads syncronization. Extraction from the queue put thread into sleep if queue empty.
// There is no limit on queue length.
//
class Thr_Queue {
public:
      Thr_Queue(void) {
	int e=pthread_mutex_init(&queue_mutex,0);
	if (e) 
		throw runtime_error("Unable to create queue mutex");
	if ((e=pthread_cond_init(&queue_cond,0)))
		throw runtime_error("Unable to create queue cond_var");
     }
     virtual ~Thr_Queue() {
     }
     // Pop data from queue back with waiting for new data.
     shared_ptr<string> getq(void);
     // Push date to queue front
     void putq(shared_ptr<string> p);

private:
  pthread_mutex_t queue_mutex;
  pthread_cond_t queue_cond;
  list<shared_ptr<string> > thr_q;
};

shared_ptr<string> 
Thr_Queue::getq(void) 
{
	int e=pthread_mutex_lock(&queue_mutex);
	if (e) 
        	throw runtime_error("Unable to lock queue mutex in getq");
        
	while(thr_q.empty())
		pthread_cond_wait(&queue_cond, &queue_mutex);

	shared_ptr<string> r=thr_q.back();
	thr_q.pop_back();
	if ((e=pthread_mutex_unlock(&queue_mutex))) 
		throw runtime_error("Unable to lock queue mutex in getq");
	return r;
}

void 
Thr_Queue::putq(shared_ptr<string> p) 
{
	int e=pthread_mutex_lock(&queue_mutex);
	if (e) {
		throw runtime_error("Unable to lock queue mutex in putq");
	}
	thr_q.push_front(p);
	if ((e=pthread_mutex_unlock(&queue_mutex))) {
		throw runtime_error("Unable to lock queue mutex");
	}
	if ((e=pthread_cond_signal(&queue_cond))) {
		throw runtime_error("Unable to signal queue condvar");
	}
}

extern "C" void* thr_start(void*);

//
// This is class for thread pool manipulation. It will run some threads. Establish queue with pointers to job data (Wav 
// filenames) which is connected to thread pool. Thread may be stopped by sending empty filename to the queue.
//

class Thr_Pool {
public:
	Thr_Pool(int n): n_thr(n),thr_q() {
	}
	virtual ~Thr_Pool() {
	}
	void run(void);

	//  Working thread get here new job (filename) or wait otherwise.
	shared_ptr<string> getq(void) {
		return thr_q.getq();
	}
	// Member function for sending job to working threads. 	
	void putq(shared_ptr<string> p) {
		thr_q.putq(p);
	}

	void int_run(void);
	void join_all(void) {
		int e;
		while(!thr_v.empty()) {
			e=pthread_join(thr_v.back(),0);
			if (e) {
				throw runtime_error("Unable to join thread");
			}
			thr_v.pop_back();	
		}
	}
	size_t thr_num(void) {
		return thr_v.size();
	}
private:

vector<pthread_t> thr_v;
int n_thr;
Thr_Queue thr_q;
};


// Helper routine for starting new thread.
extern "C" void* 
thr_start(void* p) 
{
	Thr_Pool *q=reinterpret_cast<Thr_Pool*>(p);
	q->int_run();
	pthread_exit(0);
}

//
// run member function launch new threads actualy.
//

void 
Thr_Pool::run(void) 
{
		for(int i=0;i<n_thr;++i) {
			pthread_t thr;
			if (int e=pthread_create(&thr,0,thr_start,this)) {
				throw runtime_error("Unable to start new thread");
			}
			thr_v.push_back(thr);
		}
}

//
// Working thread main loop here: get job from queue, read Wav file, encode mp3 and write new mp3 file.
//
void
Thr_Pool::int_run(void) 
{
	shared_ptr<string> job;
	do {
		job=getq();
		if (job->empty()) // Empty string is a stop signal
			break;
		try {
			Wave_Reader wr(*job);
			Encoder e(wr);
			//Transform file.wav -> file.mp3
			//It may be done with inplace character replacment i.e. *obj[job->size()-1]='3'; etc...
			job->replace(job->size()-3,3,"mp3");
			e.encode(*job);
	
		} catch (exception& e) {
			std::cerr<<"Exception: "<<e.what()<<endl;
		}
	} while(true);
}

//
// basic Wav header reader. Based on lame utility internal functions.
//
void
Wave_Reader::read_header(void) 
{
	// First is "riff" four bytes lebel.
	if (read32_hilo()!=ID_RIFF)
		throw logic_error("Not a Wave file");
	int file_size=read32_hilo();

	// "wave"  and "fmt " identificators.
	if (!((read32_hilo()==ID_WAVE) && (read32_hilo()==ID_FMT)))
		throw logic_error("Invalid Wave file");

	//Wave format Header size
	int sub_hdr_size=read32_lohi();

	format=read16_lohi();
	channels=read16_lohi();
	samples_per_sec=read32_lohi();
	avg_bytes_per_sec=read32_lohi();
	block_align=read16_lohi();
	bits_per_sample=read16_lohi();
	
	sub_hdr_size-=16;
	skip_bytes(sub_hdr_size);

	for(int next_id=read32_hilo();next_id!=ID_DATA;next_id=read32_hilo()) {
		int skip_read=read32_lohi();
		if (skip_read>0)
			skip_bytes(skip_read);
	}
	data_size=read32_lohi();
} 

void 
Encoder::encode (const string& mp3n) 
{
	vector<char> pcm;
	wave.read(pcm);			

	ofstream mp3f(mp3n.c_str(),std::ios_base::binary | std::ios_base::out);
	mp3f.exceptions ( std::ifstream::failbit | std::ifstream::badbit );

	int num_samples=pcm.size()/(wave.get_bits_per_sample()/8);

	//This size is recommended by LAME for  output buffer size
	vector<unsigned char> mp3out(((num_samples/4)*5)+7200,0); 
	int ret;
	if (wave.get_channels()==2) 
		ret=lame_encode_buffer_interleaved(gfp,reinterpret_cast<short*>(&pcm[0]),
						num_samples/2,&mp3out[0],mp3out.size());
	else
		ret=lame_encode_buffer(gfp,reinterpret_cast<short*>(&pcm[0]),0,
						num_samples,&mp3out[0],mp3out.size());

	if (ret<0)
		throw runtime_error("LAME encoding error");
	mp3f.write(reinterpret_cast<char*>(&mp3out[0]),ret);

	if ((ret=lame_encode_flush(gfp,&mp3out[0],mp3out.size()))<0){
		throw runtime_error("LAME encoding error");
	} 
	mp3f.write(reinterpret_cast<char*>(&mp3out[0]),ret);

	mp3f.close();
}



int
main(int ac,char* av[])
{
	if (ac!=2) {
		cerr<<"invalid argument: usage: "<<av[0]<<" <directory>\n";
		exit(1);
	}
	if(chdir(av[1])) {
		cerr<<"couldn't chdir to '"<<av[1]<<"'\n";
		exit(1);
	}
	DIR* dir=opendir(".");
	if (!dir) {
		cerr<<"couldn't read directory '"<<av[1]<<"'\n";
		exit(1);

	}
	//Get number of online cores.
	int ncores=4;
#ifndef __WINNT
	ncores=sysconf(_SC_NPROCESSORS_ONLN);
	if (ncores<=0) 
		ncores=4;
#endif
	int nfiles=0;
	try {
		Thr_Pool thrs(ncores);
		thrs.run();

		while (dirent* dent=readdir(dir)) {
#if  !(defined(__CYGWIN__) || defined(__WINNT))
			if (dent->d_type==DT_REG) {
#endif
				string n(dent->d_name);
				if (n.size()>=4 && tolower(n[n.size()-1])=='v' &&
					tolower(n[n.size()-2])=='a' && 
					tolower(n[n.size()-3])=='w' &&
					n[n.size()-4]=='.') {
							thrs.putq(make_shared<string>(n));
							++nfiles;
					}
#if  !(defined(__CYGWIN__) ||defined(__WINNT))
			}		
#endif
		}	
		//Send to all threads stop signal with empty string.
		for(int i=0;i<thrs.thr_num();++i) 
			thrs.putq(make_shared<string>(""));

		cout<<"Converting "<<nfiles<<" file(s) with "<<ncores<<" threads.\n";
	        // Wait for threads to exit.		
		thrs.join_all();

	} catch (exception& e) {
		cerr<<"Got exception while sending filenames: "<<e.what()<<endl;
	}
	closedir(dir);
	exit(0);
}
