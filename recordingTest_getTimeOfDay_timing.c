#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>
#include "portaudio.h"

// #define SAMPLE_RATE  (17932)    Test failure to open with this value.
#define SAMPLE_RATE  (44100)
#define FRAMES_PER_BUFFER (512)
#define NUM_SECONDS     (5)
#define NUM_CHANNELS    (2)
#define DITHER_FLAG     (0)                                     /* #define DITHER_FLAG     (paDitherOff) */
#define WRITE_TO_FILE   (1)                                     /* Set to 1 if you want to capture the recording to a file. */

// Select sample format.
#if 1
#define PA_SAMPLE_TYPE  paFloat32
typedef float SAMPLE;
#define SAMPLE_SILENCE  (0.0f)
#define PRINTF_S_FORMAT "%.8f"
#elif 1
#define PA_SAMPLE_TYPE  paInt16
typedef short SAMPLE;
#define SAMPLE_SILENCE  (0)
#define PRINTF_S_FORMAT "%d"
#elif 0
#define PA_SAMPLE_TYPE  paInt8
typedef char SAMPLE;
#define SAMPLE_SILENCE  (0)
#define PRINTF_S_FORMAT "%d"
#else
#define PA_SAMPLE_TYPE  paUInt8
typedef unsigned char SAMPLE;
#define SAMPLE_SILENCE  (128)
#define PRINTF_S_FORMAT "%d"
#endif

typedef struct
{
    int          frameIndex;                                    /* Index into sample array. */
    int          maxFrameIndex;
    SAMPLE      *recordedSamples;
}
paTestData;

int timeArr[500];
int start_t;
int tIndex = 0;

// Callback function used for recording audio
/* 
   This routine will be called by the PortAudio engine when audio is needed.
   It may be called at interrupt level on some machines so don't do anything
   that could mess up the system like calling malloc() or free().
*/
static int recordCallback( const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData )
{
    // Timer
    struct timeval currTimeVal;
    timeArr[tIndex] = (gettimeofday(&currTimeVal, NULL) - start_t);
    tIndex++;

    // Definitions
    paTestData *data = (paTestData*)userData;
    const SAMPLE *rptr = (const SAMPLE*)inputBuffer;
    SAMPLE *wptr = &data->recordedSamples[data->frameIndex * NUM_CHANNELS];
    long framesToCalc;
    long i;
    int finished;
    unsigned long framesLeft = data->maxFrameIndex - data->frameIndex;

    (void) outputBuffer;                                        /* Prevent unused variable warnings. */
    (void) timeInfo;
    (void) statusFlags;
    (void) userData;

    // Grab audio data from input device until buffer is full
    if( framesLeft < framesPerBuffer )
    {
        framesToCalc = framesLeft;
        finished = paComplete;
    }
    else
    {
        framesToCalc = framesPerBuffer;
        finished = paContinue;
    }

    if( inputBuffer == NULL )
    {
        for( i=0; i<framesToCalc; i++ )
        {
            *wptr++ = SAMPLE_SILENCE;                           /* left */
            if( NUM_CHANNELS == 2 ) *wptr++ = SAMPLE_SILENCE;   /* right */
        }
    }
    else
    {
        for( i=0; i<framesToCalc; i++ )
        {
            *wptr++ = *rptr++;                                  /* left */
            if( NUM_CHANNELS == 2 ) *wptr++ = *rptr++;          /* right */
        }
    }
    data->frameIndex += framesToCalc;
    
    return finished;                                            /* return window of audio input data */
}

// Callback function used for audio playback
/* 
   This routine will be called by the PortAudio engine when audio is needed.
   It may be called at interrupt level on some machines so don't do anything
   that could mess up the system like calling malloc() or free().
*/
static int playCallback( const void *inputBuffer, void *outputBuffer,
                         unsigned long framesPerBuffer,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void *userData )
{
    // Definitions
    paTestData *data = (paTestData*)userData;
    SAMPLE *rptr = &data->recordedSamples[data->frameIndex * NUM_CHANNELS];
    SAMPLE *wptr = (SAMPLE*)outputBuffer;
    unsigned int i;
    int finished;
    unsigned int framesLeft = data->maxFrameIndex - data->frameIndex;

    (void) inputBuffer;                                         /* Prevent unused variable warnings. */
    (void) timeInfo;    
    (void) statusFlags;
    (void) userData;

    // Load output stream until buffer is full
    if( framesLeft < framesPerBuffer )
    {
        // final buffer... 
        for( i=0; i<framesLeft; i++ )
        {
            *wptr++ = *rptr++;                                  /* left */
            if( NUM_CHANNELS == 2 ) *wptr++ = *rptr++;          /* right */
        }
        for( ; i<framesPerBuffer; i++ )
        {
            *wptr++ = 0;                                        /* left */
            if( NUM_CHANNELS == 2 ) *wptr++ = 0;                /* right */
        }
        data->frameIndex += framesLeft;
        finished = paComplete;
    }
    else
    {
        for( i=0; i<framesPerBuffer; i++ )
        {
            *wptr++ = *rptr++;                                  /* left */
            if( NUM_CHANNELS == 2 ) *wptr++ = *rptr++;          /* right */
        }
        data->frameIndex += framesPerBuffer;
        finished = paContinue;
    }
    
    return finished;                                            /* return window of audio data */
}

/*******************************************************************/
int main(void);
int main(void)
{
    // Definitions
    PaStreamParameters  inputParameters,
                        outputParameters;
    PaStream*           stream;
    PaError             err = paNoError;
    paTestData          data;
    int                 i;
    int                 totalFrames;
    int                 numSamples;
    int                 numBytes;
    SAMPLE              max, val;
    double              average;

    printf("recordingTest.c\n"); fflush(stdout);

    data.maxFrameIndex = totalFrames = NUM_SECONDS * SAMPLE_RATE;   /* Record for a few seconds. */
    data.frameIndex = 0;                                            /* Initialize frame index */  
    numSamples = totalFrames * NUM_CHANNELS;                        /* Account for stereo audio */
    numBytes = numSamples * sizeof(SAMPLE);                         /* Calculate number of bytes to allocate */
    data.recordedSamples = (SAMPLE *) malloc( numBytes );           /* From now on, recordedSamples is initialised. */
    
    if( data.recordedSamples == NULL )                              /* In case there is no audio to process */
    {
        printf("Could not allocate record array.\n");               
        goto done;
    }
    
    for( i=0; i<numSamples; i++ ) data.recordedSamples[i] = 0;      /* Initialize buffer to zeroes */

    err = Pa_Initialize();                                          /* Initialize stream */
    if( err != paNoError ) goto done;

    inputParameters.device = Pa_GetDefaultInputDevice();            /* Set device to default input device */
    if (inputParameters.device == paNoDevice) {
        fprintf(stderr,"Error: No default input device.\n");
        goto done;
    }
    
    inputParameters.channelCount = 2;                               /* stereo input */
    inputParameters.sampleFormat = PA_SAMPLE_TYPE;
    inputParameters.suggestedLatency = Pa_GetDeviceInfo( inputParameters.device )->defaultLowInputLatency;
    //inputParameters.suggestedLatency = 0;
    inputParameters.hostApiSpecificStreamInfo = NULL;

    // Record some audio
    err = Pa_OpenStream(
              &stream,
              &inputParameters,
              NULL,                                                 /* &outputParameters, */
              SAMPLE_RATE,
              paFramesPerBufferUnspecified,
              paClipOff,                                            /* we won't output out of range samples so don't bother clipping them */
              recordCallback,
              &data );
    if( err != paNoError ) goto done;

    struct timeval tvalBefore;
    start_t = gettimeofday(&tvalBefore, NULL);                                              /* get time value when stream is opened */

    err = Pa_StartStream( stream );
    if( err != paNoError ) goto done;
    printf("\n=== Now recording!! Please speak into the microphone. ===\n"); fflush(stdout);

    int timer = 0;
    int expectedIndex = 0;

    // Output frame index every 12ms, SHOULD increase by the size of FRAMES_PER_BUFFER (512) every 12ms
    printf("Frame indices printed out every 12ms. We expect that the index should increase by 512 with each line of output. \n"); 
    fflush(stdout);
    /*
    while( ( err = Pa_IsStreamActive( stream ) ) == 1 )
    {
        Pa_Sleep(12);
        timer += 12;
        expectedIndex += 512;
        printf("index at %d", timer ); printf(" ms = %d", data.frameIndex ); 
        printf(" ------ Expected index = %d\n", expectedIndex); fflush(stdout);
    }
    */
    if( err < 0 ) goto done;
    
    // Close stream
    err = Pa_CloseStream( stream );
    if( err != paNoError ) goto done;

    for(int i=0; i<500; i++)
        printf("callback time: %i\n", timeArr[i]);
    

    // Measure maximum peak amplitude.
    max = 0;
    average = 0.0;
    for( i=0; i<numSamples; i++ )
    {
        val = data.recordedSamples[i];
        if( val < 0 ) val = -val;                                   /* ABS */
        if( val > max )
        {
            max = val;
        }
        average += val;
    }

    average = average / (double)numSamples;

    printf("sample max amplitude = "PRINTF_S_FORMAT"\n", max );
    printf("sample average = %lf\n", average );

    // Write recorded data to a file.
#if WRITE_TO_FILE
    {
        FILE  *fid;
        fid = fopen("recorded.raw", "wb");
        if( fid == NULL )
        {
            printf("Could not open file.");
        }
        else
        {
            fwrite( data.recordedSamples, NUM_CHANNELS * sizeof(SAMPLE), totalFrames, fid );
            fclose( fid );
            printf("Wrote data to 'recorded.raw'\n");
        }
    }
#endif

    // Playback recorded data
    data.frameIndex = 0;

    outputParameters.device = Pa_GetDefaultOutputDevice();          /* open default audio output device */
    if (outputParameters.device == paNoDevice) {
        fprintf(stderr,"Error: No default output device.\n");
        goto done;
    }
    
    outputParameters.channelCount = 2;                              /* stereo output */
    outputParameters.sampleFormat =  PA_SAMPLE_TYPE;
    outputParameters.suggestedLatency = Pa_GetDeviceInfo( outputParameters.device )->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    printf("\n=== Now playing back. ===\n"); fflush(stdout);
    err = Pa_OpenStream(
              &stream,
              NULL,                                                 /* no input */
              &outputParameters,
              SAMPLE_RATE,
              FRAMES_PER_BUFFER,
              paClipOff,                                            /* we won't output out of range samples so don't bother clipping them */
              playCallback,
              &data );
    if( err != paNoError ) goto done;

    if( stream )
    {
        err = Pa_StartStream( stream );
        if( err != paNoError ) goto done;
        
        printf("Waiting for playback to finish.\n"); fflush(stdout);

        while( ( err = Pa_IsStreamActive( stream ) ) == 1 ) Pa_Sleep(100);
        if( err < 0 ) goto done;
        
        err = Pa_CloseStream( stream );
        if( err != paNoError ) goto done;
        
        printf("Done.\n"); fflush(stdout);
    }

done:
    Pa_Terminate();                                                 /* Terminate stream */
    if( data.recordedSamples )                                      /* Sure it is NULL or valid. */
        free( data.recordedSamples );
    if( err != paNoError )
    {
        fprintf( stderr, "An error occured while using the portaudio stream\n" );
        fprintf( stderr, "Error number: %d\n", err );
        fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
        err = 1;                                                    /* Always return 0 or 1, but no other return codes. */
    }
    return err;
}
