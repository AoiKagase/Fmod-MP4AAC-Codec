/*===============================================================================================
 CODEC_AAC.DLL

 ACC Webstream decoder by MoX

===============================================================================================*/


//#include <stdio.h>
//#include <io.h>
//#include <stdlib.h>
#include "neaacdec.h"
#include "fmod.h"
#include <string.h>
//#include "fmod_errors.h"

#ifdef WIN32
#include <malloc.h>
#else
typedef __int64_t __int64;
#endif

#ifndef MAKEFOURCC
#ifdef _BIG_ENDIAN
#define MAKEFOURCC(ch0, ch1, ch2, ch3)                              \
                ((DWORD)(BYTE)(ch3) | ((DWORD)(BYTE)(ch2) << 8) |   \
                ((DWORD)(BYTE)(ch1) << 16) | ((DWORD)(BYTE)(ch0) << 24 ))
#else
#define MAKEFOURCC(ch0, ch1, ch2, ch3)                              \
                ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) |   \
                ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24 ))
#endif
#endif

#define MAX_CHANNELS 2
#define BUFFER_SIZE (FAAD_MIN_STREAMSIZE * MAX_CHANNELS)
#define MAX_PERCENTS 384

typedef unsigned long       DWORD;
typedef int                 BOOL;
typedef unsigned char       BYTE;
typedef unsigned short      WORD;
typedef struct {
	unsigned int initbytes;
	DWORD sr;
	BYTE nch;
	NeAACDecHandle neaac;
	unsigned char fbuf[BUFFER_SIZE];
	DWORD fbuflen;
} info;

#ifdef WIN32
//#pragma comment(linker,"/ignore:4078")
//#pragma comment(linker,"/merge:.rdata=.data")
//#pragma comment(linker,"/merge:.text=.data")
//#pragma comment(linker,"/OPT:WIN98")
#endif

FMOD_RESULT F_CALLBACK aacopen(FMOD_CODEC_STATE *codec, FMOD_MODE usermode, FMOD_CREATESOUNDEXINFO *userexinfo);
FMOD_RESULT F_CALLBACK aacclose(FMOD_CODEC_STATE *codec);
FMOD_RESULT F_CALLBACK aacread(FMOD_CODEC_STATE *codec, void *buffer, unsigned int size, unsigned int *read);
FMOD_RESULT F_CALLBACK aacsetposition(FMOD_CODEC_STATE *codec, int subsound, unsigned int position, FMOD_TIMEUNIT postype);

/*
	Codec structures
*/
/*
typedef struct FMOD_CODEC_DESCRIPTION
{
	unsigned int                      apiversion;
	const char* name;
	unsigned int                      version;
	int                               defaultasstream;
	FMOD_TIMEUNIT                     timeunits;
	FMOD_CODEC_OPEN_CALLBACK          open;
	FMOD_CODEC_CLOSE_CALLBACK         close;
	FMOD_CODEC_READ_CALLBACK          read;
	FMOD_CODEC_GETLENGTH_CALLBACK     getlength;
	FMOD_CODEC_SETPOSITION_CALLBACK   setposition;
	FMOD_CODEC_GETPOSITION_CALLBACK   getposition;
	FMOD_CODEC_SOUNDCREATE_CALLBACK   soundcreate;
	FMOD_CODEC_GETWAVEFORMAT_CALLBACK getwaveformat;
} FMOD_CODEC_DESCRIPTION;
*/

FMOD_CODEC_DESCRIPTION aaccodec =
{
	0x0001,
    "FMOD AAC Codec",   // Name.
    0x00010000,                         // Version 0xAAAABBBB   A = major, B = minor.
    0,                                  // Don't force everything using this codec to be a stream,
	FMOD_TIMEUNIT_PCMBYTES,             // The time format we would like to accept into setposition/getposition.
    &aacopen,                           // Open callback.
    &aacclose,                          // Close callback.
    &aacread,                           // Read callback.
    0,                                  // Getlength callback.  (If not specified FMOD return the length in FMOD_TIMEUNIT_PCM, FMOD_TIMEUNIT_MS or FMOD_TIMEUNIT_PCMBYTES units based on the lengthpcm member of the FMOD_CODEC structure).
    &aacsetposition,                    // Setposition callback.
    0,                                  // Getposition callback. (only used for timeunit types that are not FMOD_TIMEUNIT_PCM, FMOD_TIMEUNIT_MS and FMOD_TIMEUNIT_PCMBYTES).
    0                                   // Sound create callback (don't need it)
};


static int get_AAC_format(info* x)
{
	unsigned int a=0;
	do {
#if 0
		if (*(DWORD*)(x->fbuf+a)==MAKEFOURCC('A','D','I','F')) { // "ADIF" signature
			x->initbytes+=a;
			return -1; //Not supported
		}
#endif
		// MP4 Format
		if (x->fbuf[a+4] == 'f' && x->fbuf[a+5] == 't' && x->fbuf[a+6] == 'y' && x->fbuf[a+7] == 'p')
		{
			x->initbytes += a;
			return 1;
		}

		if (x->fbuf[a]==0xff && (x->fbuf[a+1]&0xf6)==0xf0 && ((x->fbuf[a+2]&0x3C)>>2)<12) { // ADTS header syncword
			x->initbytes+=a;
			return 0;
		}
	} while (++a<x->fbuflen-4);
	return -1;
}

/*
FMODGetCodecDescription is mandatory for every fmod plugin.  This is the symbol the registerplugin function searches for.
Must be declared with F_API to make it export as stdcall.
MUST BE EXTERN'ED AS C!  C++ functions will be mangled incorrectly and not load in fmod.
*/


#ifdef __cplusplus
extern "C" {
#endif

__declspec(dllexport) FMOD_CODEC_DESCRIPTION * F_API FMODGetCodecDescription()
{
    return &aaccodec;
}

#ifdef __cplusplus
}
#endif

static FMOD_CODEC_WAVEFORMAT    aacwaveformat;
/*
    The actual codec code.

    Note that the callbacks uses FMOD's supplied file system callbacks.

    This is important as even though you might want to open the file yourself, you would lose the following benefits.
    1. Automatic support of memory files, CDDA based files, and HTTP/TCPIP based files.
    2. "fileoffset" / "length" support when user calls System::createSound with FMOD_CREATESOUNDEXINFO structure.
    3. Buffered file access.
    FMOD files are high level abstracts that support all sorts of 'file', they are not just disk file handles.
    If you want FMOD to use your own filesystem (and potentially lose the above benefits) use System::setFileSystem.
*/

FMOD_RESULT F_CALLBACK aacopen(FMOD_CODEC_STATE *codec, FMOD_MODE usermode, FMOD_CREATESOUNDEXINFO *userexinfo)
{
	if(!codec) return FMOD_ERR_INTERNAL;

    aacwaveformat.channels     = 2;
    aacwaveformat.format       = FMOD_SOUND_FORMAT_PCM16;
    aacwaveformat.frequency    = 44100;
    aacwaveformat.pcmblocksize = 4096 * 2;//aacwaveformat.channels * 2;          /* 2 = 16bit pcm */
	aacwaveformat.lengthpcm    = 0xffffffff;// codec->filesize;// / aacwaveformat.blockalign;   /* bytes converted to PCM samples */;

    codec->numsubsounds = 0;                    /* number of 'subsounds' in this sound.  For most codecs this is 0, only multi sound codecs such as FSB or CDDA have subsounds. */
    codec->waveformat   = &aacwaveformat;

	unsigned int readBytes = 0;
	unsigned int mode = 0; // 0 = AAC, 1 = MP4
	FMOD_RESULT r;
	info* x = new info;
	if (!x) return FMOD_ERR_INTERNAL;
	memset(x,0,sizeof(info));

	codec->plugindata = x;   /* user data value */
	
	r = codec->functions->read(codec, x->fbuf, BUFFER_SIZE, &readBytes);

	if (r != FMOD_OK || readBytes == 0)
		return FMOD_ERR_FILE_EOF;

	x->fbuflen += readBytes;
	x->initbytes = 0;
	if((mode = get_AAC_format(x)) == -1)
		return FMOD_ERR_FILE_BAD;
	
	if(! (x->neaac = NeAACDecOpen()))
		return FMOD_ERR_INTERNAL;

	if (x->initbytes < 0 || x->initbytes > BUFFER_SIZE)
		return FMOD_ERR_INTERNAL;

	memmove(x->fbuf, x->fbuf + x->initbytes, BUFFER_SIZE - x->initbytes);
	x->fbuflen -= x->initbytes;

	r = codec->functions->read(codec, x->fbuf + x->fbuflen, BUFFER_SIZE - x->fbuflen, &readBytes);
	if (r != FMOD_OK)
		return FMOD_ERR_FILE_EOF;
	x->fbuflen += readBytes;

	NeAACDecConfigurationPtr config;
	/* Set configuration */
	config = NeAACDecGetCurrentConfiguration(x->neaac);
	config->outputFormat = FAAD_FMT_16BIT;
	config->downMatrix = 1;
//	config->dontUpSampleImplicitSBR = 1;
	NeAACDecSetConfiguration(x->neaac, config);

	long byte = 0;
	if ((byte = NeAACDecInit2(x->neaac, x->fbuf, x->fbuflen, &x->sr, &x->nch)) < 0)
	{
		//        /* If some error initializing occured, skip the file */
		//        faad_fprintf(stderr, "Error initializing decoder library.\n");
		NeAACDecClose(x->neaac);
		//      mp4read_close();
		return FMOD_ERR_INTERNAL;
	}
	else
	{
		memmove(x->fbuf, x->fbuf + byte, BUFFER_SIZE - byte);
		x->fbuflen -= byte;
	}

	//if(! (x->neaac = NeAACDecOpen()))
	//	return FMOD_ERR_INTERNAL;

	//if (x->initbytes < 0 || x->initbytes > BUFFER_SIZE)
	//	return FMOD_ERR_INTERNAL;

	//memmove(x->fbuf, x->fbuf + x->initbytes, BUFFER_SIZE - x->initbytes);
	//x->fbuflen -= x->initbytes;

	//r = codec->functions->read(codec, x->fbuf + x->fbuflen, BUFFER_SIZE - x->fbuflen, &readBytes);
	//if (r != FMOD_OK)
	//	return FMOD_ERR_FILE_EOF;

	//x->fbuflen += readBytes;

	//long byt = NeAACDecInit(x->neaac, x->fbuf, x->fbuflen, &x->sr, &x->nch);
	//if (byt < 0)
	//	return FMOD_ERR_INTERNAL;
	//if (byt > 0) {
	//	memmove(x->fbuf, x->fbuf + byt, BUFFER_SIZE - byt);
	//	x->fbuflen -= byt;
	//}

	//NeAACDecConfigurationPtr config = NeAACDecGetCurrentConfiguration(x->neaac);
	//config->outputFormat = FAAD_FMT_16BIT;
	//config->defSampleRate = 44100;
	//NeAACDecSetConfiguration(x->neaac, config);
    return FMOD_OK;
}

FMOD_RESULT F_CALLBACK aacclose(FMOD_CODEC_STATE *codec)
{
	info* x = (info*)codec->plugindata;
	NeAACDecClose(x->neaac);
	delete(x);
	
    return FMOD_OK;
}


FMOD_RESULT F_CALLBACK aacread(FMOD_CODEC_STATE *codec, void *buffer, unsigned int size, unsigned int *read)
{
	memset(buffer, 0, size);

	if(size < (4096 * 2)) {
		*read = size;
		return FMOD_OK;
	}

	info* x = (info*)codec->plugindata;
	if(!x || !read)
		return FMOD_ERR_INTERNAL;

	void* sample_buffer = NULL;
	unsigned int buflen = 0;
	unsigned int readBytes;
	
	NeAACDecFrameInfo info;

	bool eof = false;

	FMOD_RESULT res;

	while (buflen < size || eof) {
		do {
			readBytes = 0;
			res = codec->functions->read(codec, x->fbuf + x->fbuflen, BUFFER_SIZE - x->fbuflen, &readBytes);
			if (res == FMOD_ERR_FILE_EOF)
				eof = true;
			else if (res != FMOD_OK)
				return FMOD_ERR_INTERNAL;

			x->fbuflen += readBytes;
			sample_buffer = NeAACDecDecode(x->neaac, &info, x->fbuf, x->fbuflen);
			if (info.error != 0) {
				*read = 0;
				return FMOD_ERR_FILE_BAD;
			}
			if (info.bytesconsumed > x->fbuflen) {
				x->fbuflen = 0;
			}
			else {
				x->fbuflen -= info.bytesconsumed;
				memmove(x->fbuf, x->fbuf + info.bytesconsumed, x->fbuflen); // shift remaining data to start of buffer
			}
		} while (!info.samples || !eof);
		if (info.samples != 0) {
			if (!sample_buffer)
				return FMOD_ERR_INTERNAL;
			memcpy((unsigned char*)buffer + buflen, sample_buffer, info.samples * 2);
			buflen += info.samples * 2;
		}
	}
	*read = buflen;
	if (eof) 
		return FMOD_ERR_FILE_EOF;

	return FMOD_CODEC_FILE_READ(codec, buffer, buflen, read);
}

FMOD_RESULT F_CALLBACK aacsetposition(FMOD_CODEC_STATE *codec, int subsound, unsigned int position, FMOD_TIMEUNIT postype)
{
	return FMOD_OK; FMOD_ERR_FILE_COULDNOTSEEK;
}

//static int decodeAACfile(char* aacfile, char* sndfile, char* adts_fn, int to_stdout,
//    int def_srate, int object_type, int outputFormat, int fileType,
//    int downMatrix, int infoOnly, int adts_out, int old_format,
//    float* song_length)
//{
//    int tagsize;
//    unsigned long samplerate;
//    unsigned char channels;
//    void* sample_buffer;
//
//    audio_file* aufile = NULL;
//
//    FILE* adtsFile = NULL;
//    unsigned char* adtsData;
//    int adtsDataSize;
//
//    NeAACDecHandle hDecoder;
//    NeAACDecFrameInfo frameInfo;
//    NeAACDecConfigurationPtr config;
//
//    char percents[MAX_PERCENTS];
//    int percent, old_percent = -1;
//    int bread, fileread;
//    int header_type = 0;
//    int bitrate = 0;
//    float length = 0;
//
//    int first_time = 1;
//    int retval;
//    int streaminput = 0;
//
//    aac_buffer b;
//
//    memset(&b, 0, sizeof(aac_buffer));
//
//    if (adts_out)
//    {
//        adtsFile = faad_fopen(adts_fn, "wb");
//        if (adtsFile == NULL)
//        {
//            faad_fprintf(stderr, "Error opening file: %s\n", adts_fn);
//            return 1;
//        }
//    }
//
//    if (0 == strcmp(aacfile, "-"))
//    {
//        b.infile = stdin;
//#ifdef _WIN32
//        _setmode(_fileno(stdin), O_BINARY);
//#endif
//
//    }
//    else
//    {
//        b.infile = faad_fopen(aacfile, "rb");
//        if (b.infile == NULL)
//        {
//            /* unable to open file */
//            faad_fprintf(stderr, "Error opening file: %s\n", aacfile);
//            return 1;
//        }
//    }
//
//    retval = fseek(b.infile, 0, SEEK_END);
//#ifdef _WIN32
//    if (0 == strcmp(aacfile, "-")) {
//        retval = -1;
//    }
//#endif
//    if (retval)
//    {
//        faad_fprintf(stderr, "Input not seekable %s\n", aacfile);
//        fileread = -1;
//        streaminput = 1;
//    }
//    else {
//        fileread = ftell(b.infile);
//        fseek(b.infile, 0, SEEK_SET);
//    };
//
//    if (!(b.buffer = (unsigned char*)malloc(FAAD_MIN_STREAMSIZE * MAX_CHANNELS)))
//    {
//        faad_fprintf(stderr, "Memory allocation error\n");
//        return 0;
//    }
//    memset(b.buffer, 0, FAAD_MIN_STREAMSIZE * MAX_CHANNELS);
//
//    bread = fread(b.buffer, 1, FAAD_MIN_STREAMSIZE * MAX_CHANNELS, b.infile);
//    b.bytes_into_buffer = bread;
//    b.bytes_consumed = 0;
//    b.file_offset = 0;
//
//    if (bread != FAAD_MIN_STREAMSIZE * MAX_CHANNELS)
//        b.at_eof = 1;
//
//    tagsize = 0;
//    if (!memcmp(b.buffer, "ID3", 3))
//    {
//        /* high bit is not used */
//        tagsize = (b.buffer[6] << 21) | (b.buffer[7] << 14) |
//            (b.buffer[8] << 7) | (b.buffer[9] << 0);
//
//        tagsize += 10;
//        advance_buffer(&b, tagsize);
//        fill_buffer(&b);
//    }
//
//    hDecoder = NeAACDecOpen();
//
//    /* Set the default object type and samplerate */
//    /* This is useful for RAW AAC files */
//    config = NeAACDecGetCurrentConfiguration(hDecoder);
//    if (def_srate)
//        config->defSampleRate = def_srate;
//    config->defObjectType = object_type;
//    config->outputFormat = outputFormat;
//    config->downMatrix = downMatrix;
//    config->useOldADTSFormat = old_format;
//    //config->dontUpSampleImplicitSBR = 1;
//    NeAACDecSetConfiguration(hDecoder, config);
//
//    /* get AAC infos for printing */
//    header_type = 0;
//    if (streaminput == 1)
//        lookforheader(&b);
//
//    if ((b.buffer[0] == 0xFF) && ((b.buffer[1] & 0xF6) == 0xF0))
//    {
//        if (streaminput == 1)
//        {
//            int /*frames,*/ frame_length;
//            int samplerate;
//            float frames_per_sec, bytes_per_frame;
//            channels = 2;
//            samplerate = adts_sample_rates[(b.buffer[2] & 0x3c) >> 2];
//            frame_length = ((((unsigned int)b.buffer[3] & 0x3)) << 11)
//                | (((unsigned int)b.buffer[4]) << 3) | (b.buffer[5] >> 5);
//            frames_per_sec = (float)samplerate / 1024.0f;
//            bytes_per_frame = (float)frame_length / (float)(1000);
//            bitrate = (int)(8. * bytes_per_frame * frames_per_sec + 0.5);
//            length = 1;
//            faad_fprintf(stderr, "Streamed input format  samplerate %d channels %d.\n", samplerate, channels);
//        }
//        else {
//            adts_parse(&b, &bitrate, &length);
//            fseek(b.infile, tagsize, SEEK_SET);
//
//            bread = fread(b.buffer, 1, FAAD_MIN_STREAMSIZE * MAX_CHANNELS, b.infile);
//            if (bread != FAAD_MIN_STREAMSIZE * MAX_CHANNELS)
//                b.at_eof = 1;
//            else
//                b.at_eof = 0;
//            b.bytes_into_buffer = bread;
//            b.bytes_consumed = 0;
//            b.file_offset = tagsize;
//        }
//
//        header_type = 1;
//    }
//    else if (memcmp(b.buffer, "ADIF", 4) == 0)
//    {
//        int skip_size = (b.buffer[4] & 0x80) ? 9 : 0;
//        bitrate = ((unsigned int)(b.buffer[4 + skip_size] & 0x0F) << 19) |
//            ((unsigned int)b.buffer[5 + skip_size] << 11) |
//            ((unsigned int)b.buffer[6 + skip_size] << 3) |
//            ((unsigned int)b.buffer[7 + skip_size] & 0xE0);
//
//        length = (float)fileread;
//        if (length != 0)
//        {
//            length = ((float)length * 8.f) / ((float)bitrate) + 0.5f;
//        }
//
//        bitrate = (int)((float)bitrate / 1000.0f + 0.5f);
//
//        header_type = 2;
//    }
//
//    *song_length = length;
//
//    fill_buffer(&b);
//    if ((bread = NeAACDecInit(hDecoder, b.buffer,
//        b.bytes_into_buffer, &samplerate, &channels)) < 0)
//    {
//        /* If some error initializing occured, skip the file */
//        faad_fprintf(stderr, "Error initializing decoder library.\n");
//        if (b.buffer)
//            free(b.buffer);
//        NeAACDecClose(hDecoder);
//        if (b.infile != stdin)
//            fclose(b.infile);
//        return 1;
//    }
//    advance_buffer(&b, bread);
//    fill_buffer(&b);
//
//    /* print AAC file info */
//    faad_fprintf(stderr, "%s file info:\n", aacfile);
//    switch (header_type)
//    {
//    case 0:
//        faad_fprintf(stderr, "RAW\n\n");
//        break;
//    case 1:
//        faad_fprintf(stderr, "ADTS, %.3f sec, %d kbps, %d Hz\n\n",
//            length, bitrate, samplerate);
//        break;
//    case 2:
//        faad_fprintf(stderr, "ADIF, %.3f sec, %d kbps, %d Hz\n\n",
//            length, bitrate, samplerate);
//        break;
//    }
//
//    if (infoOnly)
//    {
//        NeAACDecClose(hDecoder);
//        if (b.infile != stdin)
//            fclose(b.infile);
//        if (b.buffer)
//            free(b.buffer);
//        return 0;
//    }
//
//    do
//    {
//        sample_buffer = NeAACDecDecode(hDecoder, &frameInfo,
//            b.buffer, b.bytes_into_buffer);
//
//        if (adts_out == 1)
//        {
//            int skip = (old_format) ? 8 : 7;
//            adtsData = MakeAdtsHeader(&adtsDataSize, &frameInfo, old_format);
//
//            /* write the adts header */
//            fwrite(adtsData, 1, adtsDataSize, adtsFile);
//
//            /* write the frame data */
//            if (frameInfo.header_type == ADTS)
//                fwrite(b.buffer + skip, 1, frameInfo.bytesconsumed - skip, adtsFile);
//            else
//                fwrite(b.buffer, 1, frameInfo.bytesconsumed, adtsFile);
//        }
//
//        /* update buffer indices */
//        advance_buffer(&b, frameInfo.bytesconsumed);
//
//        /* check if the inconsistent number of channels */
//        if (aufile != NULL && frameInfo.channels != aufile->channels)
//            frameInfo.error = 12;
//
//        if (frameInfo.error > 0)
//        {
//            faad_fprintf(stderr, "Error: %s\n",
//                NeAACDecGetErrorMessage(frameInfo.error));
//        }
//
//        /* open the sound file now that the number of channels are known */
//        if (first_time && !frameInfo.error)
//        {
//            /* print some channel info */
//            print_channel_info(&frameInfo);
//
//            if (!adts_out)
//            {
//                /* open output file */
//                if (!to_stdout)
//                {
//                    aufile = open_audio_file(sndfile, frameInfo.samplerate, frameInfo.channels,
//                        outputFormat, fileType, aacChannelConfig2wavexChannelMask(&frameInfo));
//                }
//                else {
//                    aufile = open_audio_file("-", frameInfo.samplerate, frameInfo.channels,
//                        outputFormat, fileType, aacChannelConfig2wavexChannelMask(&frameInfo));
//                }
//                if (aufile == NULL)
//                {
//                    if (b.buffer)
//                        free(b.buffer);
//                    NeAACDecClose(hDecoder);
//                    if (b.infile != stdin)
//                        fclose(b.infile);
//                    return 0;
//                }
//            }
//            else {
//                faad_fprintf(stderr, "Writing output MPEG-4 AAC ADTS file.\n\n");
//            }
//            first_time = 0;
//        }
//
//        percent = min((int)(b.file_offset * 100) / fileread, 100);
//        if (percent > old_percent)
//        {
//            old_percent = percent;
//            snprintf(percents, MAX_PERCENTS, "%d%% decoding %s.", percent, aacfile);
//            faad_fprintf(stderr, "%s\r", percents);
//#ifdef _WIN32
//            SetConsoleTitle(percents);
//#endif
//        }
//
//        if ((frameInfo.error == 0) && (frameInfo.samples > 0) && (!adts_out))
//        {
//            if (write_audio_file(aufile, sample_buffer, frameInfo.samples, 0) == 0)
//                break;
//        }
//
//        /* fill buffer */
//        fill_buffer(&b);
//
//        if (b.bytes_into_buffer == 0)
//            sample_buffer = NULL; /* to make sure it stops now */
//
//    } while (sample_buffer != NULL);
//
//    NeAACDecClose(hDecoder);
//
//    if (adts_out == 1)
//    {
//        fclose(adtsFile);
//    }
//
//    if (b.infile != stdin)
//        fclose(b.infile);
//
//    if (!first_time && !adts_out)
//        close_audio_file(aufile);
//
//    if (b.buffer)
//        free(b.buffer);
//
//    return frameInfo.error;
//}
//
//static const unsigned long srates[] =
//{
//    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000,
//    12000, 11025, 8000
//};
//
//static int decodeMP4file(char* mp4file, char* sndfile, char* adts_fn, int to_stdout,
//    int outputFormat, int fileType, int downMatrix, int noGapless,
//    int infoOnly, int adts_out, float* song_length, float seek_to)
//{
//    /*int track;*/
//    unsigned long samplerate;
//    unsigned char channels;
//    void* sample_buffer;

//    long sampleId, startSampleId;

//    audio_file* aufile = NULL;
//
//    FILE* adtsFile = NULL;
//    unsigned char* adtsData;
//    int adtsDataSize;
//
//    NeAACDecHandle hDecoder;
//    NeAACDecConfigurationPtr config;
//    NeAACDecFrameInfo frameInfo;
//    mp4AudioSpecificConfig mp4ASC;
//
//    char percents[MAX_PERCENTS];
//    int percent, old_percent = -1;
//
//    int first_time = 1;
//
//    /* for gapless decoding */
//    unsigned int useAacLength = 1;
//    unsigned int framesize;
//    unsigned decoded;
//
//    if (strcmp(mp4file, "-") == 0) {
//        faad_fprintf(stderr, "Cannot open stdin for MP4 input \n");
//        return 1;
//    }
//
//    if (!quiet)
//    {
//        mp4config.verbose.header = 1;
//        mp4config.verbose.tags = 1;
//    }
//    if (mp4read_open(mp4file))
//    {
//        /* unable to open file */
//        faad_fprintf(stderr, "Error opening file: %s\n", mp4file);
//        return 1;
//    }
//
//    hDecoder = NeAACDecOpen();
//
//    /* Set configuration */
//    config = NeAACDecGetCurrentConfiguration(hDecoder);
//    config->outputFormat = outputFormat;
//    config->downMatrix = downMatrix;
//    //config->dontUpSampleImplicitSBR = 1;
//    NeAACDecSetConfiguration(hDecoder, config);
//
//    if (adts_out)
//    {
//        adtsFile = faad_fopen(adts_fn, "wb");
//        if (adtsFile == NULL)
//        {
//            faad_fprintf(stderr, "Error opening file: %s\n", adts_fn);
//            return 1;
//        }
//    }
//
//    if (NeAACDecInit2(hDecoder, mp4config.asc.buf, mp4config.asc.size,
//        &samplerate, &channels) < 0)
//    {
//        /* If some error initializing occured, skip the file */
//        faad_fprintf(stderr, "Error initializing decoder library.\n");
//        NeAACDecClose(hDecoder);
//        mp4read_close();
//        return 1;
//    }
//
//    framesize = 1024;
//    useAacLength = 0;
//    decoded = 0;
//
//    if (mp4config.asc.size)
//    {
//        if (NeAACDecAudioSpecificConfig(mp4config.asc.buf, mp4config.asc.size, &mp4ASC) >= 0)
//        {
//            if (mp4ASC.frameLengthFlag == 1) framesize = 960;
//            if (mp4ASC.sbr_present_flag == 1 || mp4ASC.forceUpSampling) framesize *= 2;
//        }
//    }
//
//    /* print some mp4 file info */
//    faad_fprintf(stderr, "%s file info:\n\n", mp4file);
//    {
//        char* tag = NULL, * item = NULL;
//        /*int k, j;*/
//        char* ot[6] = { "NULL", "MAIN AAC", "LC AAC", "SSR AAC", "LTP AAC", "HE AAC" };
//        float seconds;
//        seconds = (float)mp4config.samples / (float)mp4ASC.samplingFrequency;
//
//        *song_length = seconds;
//
//        faad_fprintf(stderr, "%s\t%.3f secs, %d ch, %d Hz\n\n", ot[(mp4ASC.objectTypeIndex > 5) ? 0 : mp4ASC.objectTypeIndex],
//            seconds, mp4ASC.channelsConfiguration, mp4ASC.samplingFrequency);
//    }
//
//    if (infoOnly)
//    {
//        NeAACDecClose(hDecoder);
//        mp4read_close();
//        return 0;
//    }
//
//    startSampleId = 0;
//    if (seek_to > 0.1)
//        startSampleId = (int64_t)(seek_to * mp4config.samplerate / framesize);
//
//    mp4read_seek(startSampleId);
//    for (sampleId = startSampleId; sampleId < mp4config.frame.ents; sampleId++)
//    {
//        /*int rc;*/
//        long dur;
//        unsigned int sample_count;
//        unsigned int delay = 0;
//
//        if (mp4read_frame())
//            break;
//
//        sample_buffer = NeAACDecDecode(hDecoder, &frameInfo, mp4config.bitbuf.data, mp4config.bitbuf.size);
//
//        if (!sample_buffer) {
//            /* unable to decode file, abort */
//            break;
//        }
//
//        if (adts_out == 1)
//        {
//            adtsData = MakeAdtsHeader(&adtsDataSize, &frameInfo, 0);
//
//            /* write the adts header */
//            fwrite(adtsData, 1, adtsDataSize, adtsFile);
//
//            fwrite(mp4config.bitbuf.data, 1, frameInfo.bytesconsumed, adtsFile);
//        }
//
//        dur = frameInfo.samples / frameInfo.channels;
//        decoded += dur;
//
//        if (decoded > mp4config.samples)
//            dur += mp4config.samples - decoded;
//
//        if (dur > framesize)
//        {
//            faad_fprintf(stderr, "Warning: excess frame detected in MP4 file.\n");
//            dur = framesize;
//        }
//
//        if (!noGapless)
//        {
//            if (useAacLength || (mp4config.samplerate != samplerate)) {
//                sample_count = frameInfo.samples;
//            }
//            else {
//                sample_count = (unsigned int)(dur * frameInfo.channels);
//                if (sample_count > frameInfo.samples)
//                    sample_count = frameInfo.samples;
//            }
//        }
//        else {
//            sample_count = frameInfo.samples;
//        }
//
//        /* open the sound file now that the number of channels are known */
//        if (first_time && !frameInfo.error)
//        {
//            /* print some channel info */
//            print_channel_info(&frameInfo);
//
//            if (!adts_out)
//            {
//                /* open output file */
//                if (!to_stdout)
//                {
//                    aufile = open_audio_file(sndfile, frameInfo.samplerate, frameInfo.channels,
//                        outputFormat, fileType, aacChannelConfig2wavexChannelMask(&frameInfo));
//                }
//                else {
//#ifdef _WIN32
//                    _setmode(_fileno(stdout), O_BINARY);
//#endif
//                    aufile = open_audio_file("-", frameInfo.samplerate, frameInfo.channels,
//                        outputFormat, fileType, aacChannelConfig2wavexChannelMask(&frameInfo));
//                }
//                if (aufile == NULL)
//                {
//                    NeAACDecClose(hDecoder);
//                    mp4read_close();
//                    return 0;
//                }
//            }
//            first_time = 0;
//        }
//
//        percent = min((int)(sampleId * 100) / mp4config.frame.ents, 100);
//        if (percent > old_percent)
//        {
//            old_percent = percent;
//            snprintf(percents, MAX_PERCENTS, "%d%% decoding %s.", percent, mp4file);
//            faad_fprintf(stderr, "%s\r", percents);
//#ifdef _WIN32
//            SetConsoleTitle(percents);
//#endif
//        }
//
//        if ((frameInfo.error == 0) && (sample_count > 0) && (!adts_out))
//        {
//            if (write_audio_file(aufile, sample_buffer, sample_count, delay) == 0)
//                break;
//        }
//
//        if (frameInfo.error > 0)
//        {
//            faad_fprintf(stderr, "Warning: %s\n",
//                NeAACDecGetErrorMessage(frameInfo.error));
//        }
//    }
//
//    NeAACDecClose(hDecoder);
//
//    if (adts_out == 1)
//    {
//        fclose(adtsFile);
//    }
//
//    mp4read_close();
//
//    if (!first_time && !adts_out)
//        close_audio_file(aufile);
//
//    return frameInfo.error;
//}
