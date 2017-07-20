#include <cctype>
#include <string>
#include <iostream>
#include <list>
#include <fstream>
#include <vector>
#include <memory>
#include <lame/lame.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <pthread.h>



using namespace std;





class Wave_Reader {
public:
	Wave_Reader(const string& wname): wave_file(wname,std::ios_base::binary | std::ios_base::in ) {
		if(!wave_file)
        		throw std::runtime_error("Unable to open file");
		wave_file.exceptions ( std::ifstream::failbit | std::ifstream::badbit );
		read_header();
		if (format!=1) 
        		throw std::runtime_error("Only PCM Wave file supported");
		
	} 
	~Wave_Reader() {
		wave_file.close();
	}
	void read_header(void); 

	// Here support routines for reading binary file.
	void skip_bytes(int s) {
		wave_file.seekg(s,ios_base::cur);
	}
	int read32_hilo(void) {
		char buf[4];
		wave_file.read(&buf[0],4);
		char rbuf[4] = { buf[3],buf[2],buf[1],buf[0]};
		return *(reinterpret_cast<int*>(&rbuf[0]));
	}
	int read32_lohi(void) {
		int32_t data;
		wave_file.read(reinterpret_cast<char*>(&data),4);
		return data;
	}
	int read16_lohi(void) {
		int16_t data;
		wave_file.read(reinterpret_cast<char*>(&data),2);
		return data;
	}
	vector<char>& read(vector<char>& d) {
		d.resize(data_size);
		wave_file.read(&d[0],data_size);
		data_size=0;
		return d;
	}


	//
	// Wave file header data entries getters functions
	//

	int get_format(void) {
		return format;
	}

	int get_channels(void) {
		return channels;
	}

	int get_bits_per_sample(void) {
		return bits_per_sample;
	}

	int get_samples_per_sec(void) {
		return samples_per_sec;
	}

	int get_avg_bytes_per_sec(void) {
		return avg_bytes_per_sec;
	}
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

class Encoder {
public:
		Encoder(Wave_Reader& wr): wave(wr) {
			  if (!(gfp = lame_init()))
				throw std::runtime_error("Unable to init LAME library");
			  
			  lame_set_num_channels(gfp,wr.get_channels());
   			  lame_set_in_samplerate(gfp,wr.get_samples_per_sec());
   			  lame_set_quality(gfp,4); /* 4 is for good quality */ 

			  // Other parameters LAME choose for us automatically (mono, brate, etc);
			  if(lame_init_params(gfp)<0)
				throw std::runtime_error("Unable to setup LAME library");
		}

		~Encoder() {
			lame_close(gfp);
		}
	
		void encode (const string& mp3n);
private:	
 lame_global_flags *gfp;
 Wave_Reader& wave;
};

extern "C" void* thr_start(void*);

class Thr_Queue {
public:
	Thr_Queue(int n): n_thr(n) {
		int e=pthread_mutex_init(&queue_mutex,0);
		if (e) 
			throw runtime_error("Unable to create queue mutex");
		if ((e=pthread_cond_init(&queue_cond,0)))
			throw runtime_error("Unable to create queue cond_var");
		
	}

	void run(void) {
		for(int i=0;i<n_thr;++i) {
			pthread_t thr;
			if (int e=pthread_create(&thr,0,thr_start,this)) {
				throw runtime_error("Unable to start new thread");
			}
			thr_v.push_back(thr);
		}
	}

	shared_ptr<string> getq(void);
	void putq(shared_ptr<string> p);
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

pthread_mutex_t queue_mutex;
pthread_cond_t queue_cond;
list<shared_ptr<string> > thr_q;
vector<pthread_t> thr_v;
int n_thr;
};

extern "C" void* 
thr_start(void* p) 
{
	Thr_Queue *q=reinterpret_cast<Thr_Queue*>(p);
	q->int_run();
	pthread_exit(0);
}
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

void
Thr_Queue::int_run(void) 
{
	shared_ptr<string> job;
	do {
		job=getq();
		if (job->empty()) // Empty string is a stop signal
			break;
		try {
			Wave_Reader wr(*job);

			cout<<"Channels: "<<wr.get_channels()
			<<" Format: "<<wr.get_format()
		        <<"\nBites per sample: "<<wr.get_bits_per_sample()
			<<" Samples per second: "<<wr.get_samples_per_sec()
			<<"\nAvg bytes per sec: "<<wr.get_avg_bytes_per_sec()
			<<"\nData size: "<<wr.get_size()<<endl;	

			Encoder e(wr);
			job->replace(job->size()-3,3,"mp3");
			e.encode(*job);
	
		} catch (std::exception& e) {
			std::cout<<"Exception: "<<e.what()<<endl;
		}
	} while(true);
}

void
Wave_Reader::read_header(void) 
{
	// First is "riff" four bytes lebel.
	if (read32_hilo()!=ID_RIFF)
		throw std::logic_error("Not a Wave file");
	int file_size=read32_hilo();

	// "wave"  and "fmt " identificators.
	if (!((read32_hilo()==ID_WAVE) && (read32_hilo()==ID_FMT)))
		throw std::logic_error("Invalid Wave file");

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

	ofstream mp3f(mp3n,std::ios_base::binary | std::ios_base::out);
	mp3f.exceptions ( std::ifstream::failbit | std::ifstream::badbit );

	int num_samples=pcm.size()/(wave.get_bits_per_sample()/8);

	//This size recommended by LAME for  output buffer size
	vector<unsigned char> mp3out(((num_samples/4)*5)+7200,0); 
	int ret;
	if (wave.get_channels()==2) 
		ret=lame_encode_buffer_interleaved(gfp,reinterpret_cast<short*>(&pcm[0]),
						num_samples/2,&mp3out[0],mp3out.size());
	else
		ret=lame_encode_buffer(gfp,reinterpret_cast<short*>(&pcm[0]),0,
						num_samples,&mp3out[0],mp3out.size());

	cout<<"Lame encoded "<<pcm.size()<<" bytes Wav into "<<ret<<" bytes\n";
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
	int ncores=sysconf(_SC_NPROCESSORS_ONLN);
	if (ncores<=0) 
		ncores=2;
	try {
		Thr_Queue thrs(ncores);
		thrs.run();

		while (dirent* dent=readdir(dir)) {
			if (dent->d_type==DT_REG) {
				string n(dent->d_name);
				if (n.size()>=4 && tolower(n[n.size()-1])=='v' &&
					tolower(n[n.size()-2])=='a' && 
					tolower(n[n.size()-3])=='w' &&
					n[n.size()-4]=='.') {
							thrs.putq(make_shared<string>(n));
					}
			}		
		}	
		//Send to all threads stop signal with empty string.
		for(int i=0;i<thrs.thr_num();++i) 
			thrs.putq(make_shared<string>(""));

	        // Wait for threads to exit.		
		thrs.join_all();

	} catch (exception& e) {
		cerr<<"Got exception while sending filenames: "<<e.what()<<endl;
	}
	closedir(dir);
	exit(0);
}
