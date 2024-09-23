#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <time.h>
#if (!defined WIN32 && !defined _WIN32) || defined(__MINGW32__)
#include <unistd.h>
#else
#include <process.h>
#define getpid _getpid
#endif
#include "opus_multistream.h"
#include "opus.h"
#include "../src/opus_private.h"
#include "test_opus_common.h"

#define MAX_PACKET (1500)
#define PI (3.141592653589793238462643)
#define RAND_SAMPLE(a) (a[fast_rand() % sizeof(a)/sizeof(a[0])])
#define RMS_THRESH (0.1f)

#define SINE_SWEEP_AMPLITUDE (0.5f)
#define SINE_SWEEP_DURATION_S (60.0f)

typedef struct {
   void* encoder;
   void* decoder;
   int sample_rate;
   int num_channels;
   int frame_size;
   int float_encode;
   int float_decode;
   int custom_encode;
   int custom_decode;
} TestCustomParams;

void* generate_sine_sweep(double amplitude, int bit_depth, int sample_rate, int channels, int use_float, double duration_seconds, int* num_samples_out) {
   int i;
   int num_samples;
   double start_freq = 100.0;
   double end_freq = sample_rate / 2.0;

   num_samples = (int)floor(.5f + duration_seconds * sample_rate);

   /* Calculate the maximum sample value based on bit depth. */
   int64_t max_sample_value = (1LL << (bit_depth - 1)) - 1;

   /* Allocate memory for the output buffer. */
   if (use_float) bit_depth = 32;
   int bytes_per_sample = (bit_depth == 16) ? 2 : 4;
   void* output_buffer = malloc(num_samples * channels * bytes_per_sample);
   if (output_buffer == NULL) {
      fprintf(stderr, "Error allocating memory for output buffer.\n");
      *num_samples_out = 0;
      return NULL;
   }

   /* Generate the sine sweep/ */
   for (i = 0; i < num_samples; i++) {
      /* Calculate the time in seconds for the current sample */
      double t = (double)i / sample_rate;

      /* Calculate the frequency at this time point */
      double b = log((end_freq + start_freq) / start_freq) / duration_seconds;
      double a = start_freq / b;

      double sample = amplitude * sin(2 * PI * a * exp(b * t) - (b * t) - 1);

      if (use_float) {
         float* output = (float*)output_buffer;
         output[i * channels] = (float)sample;
         if (channels == 2) {
            output[i * channels + 1] = output[i * channels];
         }
      }
      else {
         /* Scale and convert to the appropriate integer type based on bit depth */
         if (bit_depth == 16) {
            int16_t* output = (int16_t*)output_buffer;
            output[i * channels] = (int16_t)floor(.5f + sample * max_sample_value);
            if (channels == 2) {
               output[i * channels + 1] = output[i * channels];
            }
         }
         else if (bit_depth == 24) {
            /* Assuming 24-bit samples are stored in 32-bit integers. */
            int32_t* output = (int32_t*)output_buffer;
            output[i * channels] = (int32_t)floor(.5f + sample * max_sample_value) << 8;
            if (channels == 2) {
               output[i * channels + 1] = output[i * channels];
            }
         }
      }
   }

   *num_samples_out = num_samples;
   return output_buffer;
}

int test_encode(TestCustomParams params) {
   int samp_count = 0;
   void *inbuf;
   void *outbuf;
   OpusEncoder* enc = NULL;
   OpusDecoder* dec = NULL;
   OpusCustomEncoder* encC = NULL;
   OpusCustomDecoder* decC = NULL;
   unsigned char packet[MAX_PACKET+257];
   int len;
   int input_samples;
   int samples_decoded;
   int ret = 0;
#ifdef RESYNTH
   int i;
   double rmsd = 0;
#endif

   int num_channels = params.num_channels;
   int frame_size = params.frame_size;

   /* Generate input data */
   inbuf = generate_sine_sweep(SINE_SWEEP_AMPLITUDE,
                               16,
                               params.sample_rate,
                               num_channels,
                               params.float_encode,
                               SINE_SWEEP_DURATION_S,
                               &input_samples);

   /* Allocate memory for output data */
   if (params.float_decode) {
       outbuf = malloc(input_samples*num_channels*sizeof(float));
   }
   else {
       outbuf = malloc(input_samples*num_channels*sizeof(opus_int16));
   }

   if (params.custom_encode) {
      encC = (OpusCustomEncoder*)params.encoder;
   }
   else {
      enc = (OpusEncoder*)params.encoder;
   }

   if (params.custom_decode) {
      decC = (OpusCustomDecoder*)params.decoder;
   }
   else {
      dec = (OpusDecoder*)params.decoder;
   }

   /* Encode data, then decode for sanity check */
   do {
#ifndef DISABLE_FLOAT_API
      if (params.float_encode) {
         float* input = (float*)inbuf;
         if (params.custom_encode) {
            len = opus_custom_encode_float(encC,
                                           &input[samp_count*num_channels],
                                           frame_size,
                                           packet,
                                           MAX_PACKET);
            if (len <= 0) {
                fprintf(stderr, "opus_custom_encode_float() failed: %s\n", opus_strerror(len));
                ret = -1;
                break;
            }
         }
         else {
            len = opus_encode_float(enc,
                                    &input[samp_count*num_channels],
                                    frame_size,
                                    packet,
                                    MAX_PACKET);
            if (len <= 0) {
                fprintf(stderr, "opus_encode_float() failed: %s\n", opus_strerror(len));
                ret = -1;
                break;
            }
         }
      } else
#endif
      {
         opus_int16* input = (opus_int16*)inbuf;
         if (params.custom_encode) {
            len = opus_custom_encode(encC,
                                     &input[samp_count*num_channels],
                                     frame_size,
                                     packet,
                                     MAX_PACKET);
            if (len <= 0) {
                fprintf(stderr, "opus_custom_encode() failed: %s\n", opus_strerror(len));
                ret = -1;
                break;
            }
         }
         else {
            len = opus_encode(enc,
                              &input[samp_count*num_channels],
                              frame_size,
                              packet,
                              MAX_PACKET);
            if (len <= 0) {
                fprintf(stderr, "opus_encode() failed: %s\n", opus_strerror(len));
                ret = -1;
                break;
            }
         }
      }

#ifndef DISABLE_FLOAT_API
      if (params.float_decode) {
         float* output = (float*)outbuf;
         if (params.custom_decode) {
            samples_decoded = opus_custom_decode_float(decC,
                                                       packet,
                                                       len,
                                                       &output[samp_count*num_channels],
                                                       frame_size);
            if (samples_decoded != frame_size) {
                fprintf(stderr, "opus_custom_decode_float() returned %d\n", samples_decoded);
                ret = -1;
                break;
            }

         }
         else {
            samples_decoded = opus_decode_float(dec,
                                                packet,
                                                len,
                                                &output[samp_count*num_channels],
                                                frame_size,
                                                0);
            if (samples_decoded != frame_size) {
                fprintf(stderr, "opus_decode_float() returned %d\n", samples_decoded);
                ret = -1;
                break;
            }
         }
      } else
#endif
      {
         opus_int16* output = (opus_int16*)outbuf;
         if (params.custom_decode) {
            samples_decoded = opus_custom_decode(decC,
                                                 packet,
                                                 len,
                                                 &output[samp_count*num_channels],
                                                 frame_size);

            if (samples_decoded != frame_size) {
                fprintf(stderr, "opus_custom_decode() returned %d\n", samples_decoded);
                ret = -1;
                break;
            }
         }
         else {
            samples_decoded = opus_decode(dec,
                                          packet,
                                          len,
                                          &output[samp_count*num_channels],
                                          frame_size,
                                          0);
            if (samples_decoded != frame_size) {
                fprintf(stderr, "opus_decode() returned %d\n", samples_decoded);
                ret = -1;
                break;
            }
         }
      }

#ifdef RESYNTH
      if (params.float_encode) {
         float* input = (float*)inbuf;
         float* output = (float*)outbuf;
         for (i = 0; i < samples_decoded * num_channels; i++) {
            rmsd += (input[i]-output[i])*(input[i]-output[i]);
         }
      }
      else {
         opus_int16* input = (opus_int16*)inbuf;
         opus_int16* output = (opus_int16*)outbuf;
         for (i = 0; i < samples_decoded * num_channels; i++) {
            rmsd += (input[i]-output[i])*1.0*(input[i]-output[i]);
         }
      }
#endif

      samp_count += frame_size;
   } while (samp_count < input_samples);

#ifdef RESYNTH
   if (params.float_encode) {
      rmsd = sqrt(rmsd/(frame_size*num_channels*samp_count));
   }
   else {
      rmsd = sqrt(rmsd/(1.0*frame_size*num_channels*samp_count));
   }

   if (params.float_encode && (rmsd > RMS_THRESH)) {
      fprintf(stderr, "Error: encoder doesn't match decoder\n");
      fprintf(stderr, "RMS mismatch is %f\n", rmsd);
      ret = -1;
   }
   else {
      fprintf(stderr, "Encoder matches decoder!!\n");
   }
#endif

   /* Clean up */
   free(inbuf);
   free(outbuf);
   return ret;
}

void test_opus_custom(const int num_encoders, const int num_setting_changes) {
   OpusCustomMode* mode = NULL;
   OpusCustomEncoder* encC = NULL;
   OpusCustomDecoder* decC = NULL;
   OpusEncoder* enc = NULL;
   OpusDecoder* dec = NULL;
   int i, j, err;
   TestCustomParams params = {0};

   /* Parameters to fuzz. Some values are duplicated to increase their probability of being tested. */
   int sampling_rates[5] = { 8000, 12000, 16000, 24000, 48000 };
   int channels[2] = { 1, 2 };
   int bitrates[10] = { 6000, 12000, 16000, 24000, 32000, 48000, 64000, 96000, 510000, OPUS_BITRATE_MAX };
   int use_vbr[3] = { 0, 1, 1 };
   int vbr_constraints[3] = { 0, 1, 1 };
   int complexities[11] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
   int packet_loss_perc[4] = { 0, 1, 2, 5 };
   int lsb_depths[2] = { 8, 24 };
   int frame_sizes_ms_x2[4] = { 5, 10, 20, 40 };  /* x2 to avoid 2.5 ms */
#ifndef DISABLE_FLOAT_API
   int use_float_encode[2] = {0, 1};
   int use_float_decode[2] = {0, 1};
#endif
   int use_custom_encode[2] = {0, 1};
   int use_custom_decode[2] = {0, 1};

   for (i = 0; i < num_encoders; i++) {
      params.sample_rate = RAND_SAMPLE(sampling_rates);
      params.custom_encode = 1;
      params.custom_decode = 1;
      /* Can only mix and match Opus and OpusCustom with 48kHz */
      if (params.sample_rate == 48000) {
         params.custom_encode = RAND_SAMPLE(use_custom_encode);
         params.custom_decode = RAND_SAMPLE(use_custom_decode);

         /* No point in testing this as OpusCustom isn't involved */
         if (!(params.custom_encode || params.custom_decode))
            continue;
      }
      params.num_channels = RAND_SAMPLE(channels);
      int frame_size_ms_x2 = RAND_SAMPLE(frame_sizes_ms_x2);
      params.frame_size = frame_size_ms_x2 * params.sample_rate / 2000;

      /* OpusCustom isn't supporting this case at the moment (frame < 40) */
      if ((params.sample_rate == 8000 || params.sample_rate == 12000) && frame_size_ms_x2 == 5)
         continue;

      if (params.custom_encode || params.custom_decode) {
         mode = opus_custom_mode_create(params.sample_rate, params.frame_size, &err);
         if (err != OPUS_OK || mode == NULL) {
            fprintf(stderr,
                    "test_opus_custom error: %d kHz, %d ch, "
                    "custom_encode: %d, custom_decode: %d, (%d/2) ms\n",
                    params.sample_rate / 1000, params.num_channels,
                    params.custom_encode, params.custom_decode, frame_size_ms_x2);
            test_failed();
         }
      }

      if (params.custom_decode) {
         decC = opus_custom_decoder_create(mode, params.num_channels, &err);
         if (err != OPUS_OK || decC == NULL) {
            fprintf(stderr,
                    "test_opus_custom error: %d kHz, %d ch, "
                    "custom_encode: %d, custom_decode: %d, (%d/2) ms\n",
                    params.sample_rate / 1000, params.num_channels,
                    params.custom_encode, params.custom_decode, frame_size_ms_x2);
            test_failed();
         }
         params.decoder = (void*)decC;
      }
      else {
         dec = opus_decoder_create(params.sample_rate, params.num_channels, &err);
         if (err != OPUS_OK || dec == NULL) {
            fprintf(stderr,
                    "test_opus_custom error: %d kHz, %d ch, "
                    "custom_encode: %d, custom_decode: %d, (%d/2) ms\n",
                    params.sample_rate / 1000, params.num_channels,
                    params.custom_encode, params.custom_decode, frame_size_ms_x2);
            test_failed();
         }
         params.decoder = (void*)dec;
      }

      if (params.custom_encode) {
         encC = opus_custom_encoder_create(mode, params.num_channels, &err);
         if (err != OPUS_OK || encC == NULL) {
            fprintf(stderr,
                    "test_opus_custom error: %d kHz, %d ch, "
                    "custom_encode: %d, custom_decode: %d, (%d/2) ms\n",
                    params.sample_rate / 1000, params.num_channels,
                    params.custom_encode, params.custom_decode, frame_size_ms_x2);
            test_failed();
         }
         params.encoder = (void*)encC;
      }
      else {
         enc = opus_encoder_create(params.sample_rate, params.num_channels, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
         if (err != OPUS_OK || enc == NULL) {
            fprintf(stderr,
                    "test_opus_custom error: %d kHz, %d ch, "
                    "custom_encode: %d, custom_decode: %d, (%d/2) ms\n",
                    params.sample_rate / 1000, params.num_channels,
                    params.custom_encode, params.custom_decode, frame_size_ms_x2);
            test_failed();
         }
         params.encoder = (void*)enc;
      }

      for (j = 0; j < num_setting_changes; j++) {
         int bitrate = RAND_SAMPLE(bitrates);
         int vbr = RAND_SAMPLE(use_vbr);
         int vbr_constraint = RAND_SAMPLE(vbr_constraints);
         int complexity = RAND_SAMPLE(complexities);
         int pkt_loss = RAND_SAMPLE(packet_loss_perc);
         int lsb_depth = RAND_SAMPLE(lsb_depths);
#ifndef DISABLE_FLOAT_API
         params.float_encode = RAND_SAMPLE(use_float_encode);
         params.float_decode = RAND_SAMPLE(use_float_decode);
#else
         params.float_encode = 0;
         params.float_decode = 0;
#endif
#ifdef RESYNTH
         /* Resynth logic works best when encoder/decoder use same datatype */
         params.float_decode = params.float_encode;
#endif

         if (params.custom_encode) {
            if (opus_custom_encoder_ctl(encC, OPUS_SET_BITRATE(bitrate)) != OPUS_OK) test_failed();
            if (opus_custom_encoder_ctl(encC, OPUS_SET_VBR(vbr)) != OPUS_OK) test_failed();
            if (opus_custom_encoder_ctl(encC, OPUS_SET_VBR_CONSTRAINT(vbr_constraint)) != OPUS_OK) test_failed();
            if (opus_custom_encoder_ctl(encC, OPUS_SET_COMPLEXITY(complexity)) != OPUS_OK) test_failed();
            if (opus_custom_encoder_ctl(encC, OPUS_SET_PACKET_LOSS_PERC(pkt_loss)) != OPUS_OK) test_failed();
            if (opus_custom_encoder_ctl(encC, OPUS_SET_LSB_DEPTH(lsb_depth)) != OPUS_OK) test_failed();
         }
         else {
            if (opus_encoder_ctl(enc, OPUS_SET_BITRATE(bitrate)) != OPUS_OK) test_failed();
            if (opus_encoder_ctl(enc, OPUS_SET_VBR(vbr)) != OPUS_OK) test_failed();
            if (opus_encoder_ctl(enc, OPUS_SET_VBR_CONSTRAINT(vbr_constraint)) != OPUS_OK) test_failed();
            if (opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(complexity)) != OPUS_OK) test_failed();
            if (opus_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC(pkt_loss)) != OPUS_OK) test_failed();
            if (opus_encoder_ctl(enc, OPUS_SET_LSB_DEPTH(lsb_depth)) != OPUS_OK) test_failed();
         }
         fprintf(stderr,
                 "test_opus_custom: %d kHz, %d ch, float_encode: %d, float_decode: %d, "
                 "custom_encode: %d, custom_decode: %d, %d bps, vbr: %d, vbr constraint: %d, complexity: %d, "
                 "pkt loss: %d%%, lsb depth: %d, (%d/2) ms\n",
                 params.sample_rate / 1000, params.num_channels, params.float_encode, params.float_decode,
                 params.custom_encode, params.custom_decode, bitrate, vbr, vbr_constraint, complexity,
                 pkt_loss, lsb_depth, frame_size_ms_x2);
         if (test_encode(params)) {
            fprintf(stderr,
                    "test_opus_custom error: %d kHz, %d ch, float_encode: %d, float_decode: %d, "
                    "custom_encode: %d, custom_decode: %d, %d bps, vbr: %d, vbr constraint: %d, complexity: %d, "
                    "pkt loss: %d%%, lsb depth: %d, (%d/2) ms\n",
                    params.sample_rate / 1000, params.num_channels, params.float_encode, params.float_decode,
                    params.custom_encode, params.custom_decode, bitrate, vbr, vbr_constraint, complexity,
                    pkt_loss, lsb_depth, frame_size_ms_x2);
            test_failed();
         }
      }

      if (params.custom_encode || params.custom_decode) {
         opus_custom_mode_destroy(mode);
      }
      if (params.custom_decode) {
         opus_custom_decoder_destroy(decC);
      }
      else {
         opus_decoder_destroy(dec);
      }
      if (params.custom_encode) {
         opus_custom_encoder_destroy(encC);
      }
      else {
         opus_encoder_destroy(enc);
      }
   }
}

int main(int _argc, char **_argv) {
   int args = 1;
   char * strtol_str = NULL;
   const char * env_seed;
   int env_used;
   int num_encoders_to_fuzz = 5;
   int num_setting_changes = 40;

   /* Seed the random fuzz settings */
   env_used=0;
   env_seed=getenv("SEED");
   if (_argc > 1)
       iseed = strtol(_argv[1], &strtol_str, 10);  /* the first input argument might be the seed */
   if(strtol_str!=NULL && strtol_str[0]=='\0')   /* iseed is a valid number */
      args++;
   else if(env_seed) {
      iseed=atoi(env_seed);
      env_used=1;
   }
   else iseed=(opus_uint32)time(NULL)^(((opus_uint32)getpid()&65535)<<16);
   Rw=Rz=iseed;

   fprintf(stderr,"Testing extensions. Random seed: %u (%.4X)\n", iseed, fast_rand() % 65535);
   if(env_used)fprintf(stderr,"  Random seed set from the environment (SEED=%s).\n", env_seed);

   fprintf(stderr,"Testing various Opus/OpusCustom combinations "
#ifdef RESYNTH
           "with RMS validation "
#endif
           "across %d encoder(s) and %d setting change(s) each.\n", num_encoders_to_fuzz, num_setting_changes);
   test_opus_custom(num_encoders_to_fuzz, num_setting_changes);

   fprintf(stderr,"Tests completed successfully.\n");

   return 0;
}
