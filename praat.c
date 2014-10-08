
#include <stdio.h>
#include <speex/speex.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <alsa/asoundlib.h>


snd_pcm_t *alsa_open(const char *devname, unsigned samplerate, snd_pcm_uframes_t fragsize, unsigned nfrags, int direction);


int main(int argc, char **argv)
{
	snd_pcm_t *pcm_play;
	snd_pcm_t *pcm_rec;
	SpeexBits enc_bits; 
	SpeexBits dec_bits; 
	void *enc_state;
	void *dec_state;
	int frame_size;
	int r;
	struct pollfd *pfds;

	speex_bits_init(&enc_bits);
	speex_bits_init(&dec_bits);
	enc_state = speex_encoder_init(&speex_wb_mode); 
	dec_state = speex_decoder_init(&speex_wb_mode); 

	speex_encoder_ctl(enc_state,SPEEX_GET_FRAME_SIZE,&frame_size); 

	pcm_rec = alsa_open("default", 22050, frame_size, 8, SND_PCM_STREAM_CAPTURE);
	pcm_play = alsa_open("default", 22050, frame_size, 8, SND_PCM_STREAM_PLAYBACK);

	struct sockaddr_in sa;
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(9421);
	sa.sin_addr.s_addr = inet_addr("82.94.235.106");
	
	int sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
		
	int nfds = snd_pcm_poll_descriptors_count(pcm_rec) + 1;
	pfds = calloc(nfds, sizeof *pfds);
	snd_pcm_poll_descriptors(pcm_rec,  pfds, nfds-1);

	pfds[nfds-1].fd = sock;
	pfds[nfds-1].events = POLLIN;
	
	for(;;) {

		snd_pcm_prepare(pcm_rec); 
		snd_pcm_prepare(pcm_play); 
		snd_pcm_reset(pcm_rec); 
		snd_pcm_reset(pcm_play); 
		snd_pcm_start(pcm_play);
		snd_pcm_start(pcm_rec);

		int xrun = 0;

		while(!xrun) {
			
			unsigned short revents;
			char data[32768];
			int16_t audio[frame_size];

			r = poll(pfds, nfds, -1);

			/*
			 * Read socket data 
			 */

			if(pfds[nfds-1].revents & POLLIN) {
				int r = recv(sock, data, sizeof data, 0);
				if(r == -1) {
					fprintf(stderr, "recv(): %s\n", strerror(errno));
					exit(1);
				}
				speex_bits_read_from(&dec_bits, data, r);
				r = speex_decode_int(dec_state, &dec_bits, audio); 
				
				r = snd_pcm_writei(pcm_play, audio, frame_size);
			}

			/*
			 * Read audio data
			 */

			snd_pcm_poll_descriptors_revents(pcm_rec, pfds, nfds-1, &revents);

			if(revents & POLLERR) {
				xrun = 1;
			}

			if(revents & POLLIN) {
				r = snd_pcm_readi(pcm_rec, audio, frame_size);

				speex_bits_reset(&enc_bits);
				speex_encode_int(enc_state, audio, &enc_bits);
				r = speex_bits_write(&enc_bits, data, sizeof(data));

				if(r > 0) {
					r = sendto(sock, data, r, 0, (struct sockaddr *)&sa, sizeof sa);
					if(r == -1) {
						fprintf(stderr, "send(): %s\n", strerror(errno));
						exit(1);
					}
				}

			}
		}
	}

	return 0;
}


#define TRY(fn, args...) { \
                int r = fn(args); \
                if(r < 0) fprintf(stderr, "Error %s: %s\n", #fn, snd_strerror(r)); \
        }


snd_pcm_t *alsa_open(const char *devname, unsigned samplerate, snd_pcm_uframes_t fragsize, unsigned nfrags, int direction)
{
	snd_pcm_t *pcm;
	snd_pcm_hw_params_t *hwparams;
	snd_pcm_sw_params_t *swparams;
	int r;

    	r = snd_pcm_open(&pcm, devname, direction, SND_PCM_NONBLOCK);

	if(r < 0) {
		fprintf(stderr, "Error opening Alsa device '%s': %s\n", devname, snd_strerror(r));
		return NULL;
	}

	/* Hardware params */
	 
	snd_pcm_hw_params_alloca(&hwparams);
	TRY(snd_pcm_hw_params_any, pcm, hwparams);
	TRY(snd_pcm_hw_params_set_access, pcm, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED);
    	TRY(snd_pcm_hw_params_set_format, pcm, hwparams, SND_PCM_FORMAT_S16_LE);
  	TRY(snd_pcm_hw_params_set_rate_near, pcm, hwparams, &samplerate, 0);
       	TRY(snd_pcm_hw_params_set_channels, pcm, hwparams, 1);
	TRY(snd_pcm_hw_params_set_period_size_near, pcm, hwparams, &fragsize, 0);
	TRY(snd_pcm_hw_params_set_periods_near, pcm, hwparams, &nfrags, 0);
        TRY(snd_pcm_hw_params, pcm, hwparams);

	/* Software params */
	
	snd_pcm_sw_params_alloca(&swparams);
	TRY(snd_pcm_sw_params_current, pcm, swparams);
	TRY(snd_pcm_sw_params_set_avail_min, pcm, swparams, fragsize);
	TRY(snd_pcm_sw_params, pcm, swparams);

	snd_output_t *out = NULL;
	snd_output_buffer_open(&out); 
	snd_pcm_dump_setup(pcm, out);
	char *s;
	snd_output_buffer_string(out, &s);
	//printf("%s\n", s);
	snd_output_close(out);

	return pcm;
}

/*
 * End
 */

