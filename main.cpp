#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <vector>
//#include <winsock2.h>
//#include "fmod_errors.h"
#include "neaacdec.h"
#include "fmod.h"

#define MAX_CHANNELS 8
#define BUFFER_SIZE (FAAD_MIN_STREAMSIZE * MAX_CHANNELS)
#define MAX_PERCENTS 384

typedef unsigned long       DWORD;
typedef int                 BOOL;
typedef unsigned char       BYTE;
typedef unsigned short      WORD;

typedef struct {
    BYTE size[4];
    char header[4];
    BYTE* data;

} MP4HEADER;

typedef struct {
    DWORD sample_rates;
    BYTE channels;
    NeAACDecHandle aac;
    unsigned char buffer[BUFFER_SIZE];
    DWORD buflen;
    unsigned int initbytes;

} info;
static FMOD_CODEC_WAVEFORMAT    aacwaveformat;

unsigned int _get_frame_length(const char* aac_header)
{
    unsigned int len = *(unsigned int*)(aac_header + 3);
    //  len = ntohl(len); //Little Endian
    len = len >> 6;
    len = len << 19;
    return len;
}

std::uint32_t _get_size(const BYTE* size)
{
    std::uint32_t x = 0;

    for (size_t i = 0; i < sizeof(std::uint32_t); i++)
    {
        const std::uint8_t bit_shifts = (sizeof(std::uint32_t) - 1 - i) * 8;
        x |= (std::uint32_t)size[i] << bit_shifts;
    }
    return x;
}

std::uint64_t _get_size_64(const BYTE* size)
{
    std::uint64_t x = 0;

    for (size_t i = 0; i < sizeof(std::uint64_t); i++)
    {
        const std::uint8_t bit_shifts = (sizeof(std::uint64_t) - 1 - i) * 8;
        x |= (std::uint64_t)size[i] << bit_shifts;
    }
    return x;
}

static int get_AAC_format(info* x)
{
    unsigned int a = 0;
    do {
#if 0
        if (*(DWORD*)(x->buffer + a) == MAKEFOURCC('A', 'D', 'I', 'F')) { // "ADIF" signature
            x->initbytes += a;
            return -1; //Not supported
        }
#endif
        if (x->buffer[a] == 0xff && (x->buffer[a + 1] & 0xf6) == 0xf0 && ((x->buffer[a + 2] & 0x3C) >> 2) < 12) { // ADTS header syncword
            x->initbytes += a;
            return 0;
        }
    } while (++a < x->buflen - 4);
    return -1;
}

// openコールバック関数は、ファイルからデータを読み込んでデコードするために使用されます。
FMOD_RESULT F_CALLBACK myCodec_open(FMOD_CODEC_STATE* state, FMOD_MODE usermode, FMOD_CREATESOUNDEXINFO* userexinfo)
{
    // ファイルからデータを読み込んで、state->plugindataに保存する

    if (!state) 
        return FMOD_ERR_INTERNAL;

    unsigned int length_;
    unsigned int readBytes = 0;
    state->functions->size(state, &length_);

    // MP4 HEADER
    MP4HEADER* chunk = new MP4HEADER;

    FMOD_RESULT r;
    std::uint32_t position = 0;
    std::uint64_t size = 0;
    BYTE bytes[8];

    r = state->functions->read(state, chunk->size, 4, &readBytes);
    r = state->functions->read(state, chunk->header, 4, &readBytes);
    // TODO: リトルエンディアン考慮しないとsizeが取れない（[\0][\0][\0][32])
    size = _get_size(chunk->size);

    chunk->data = new BYTE[size - 8];
    // Get File Type
    r = state->functions->read(state, chunk->data, size - 8, &readBytes);

    if (!(chunk->header[0] == 'f' && chunk->header[1] == 't' && chunk->header[2] == 'y' && chunk->header[3] == 'p'))
    {
        return FMOD_ERR_FORMAT;
    }

    if (!(chunk->data[0] == 'M' && chunk->data[1] == '4' && chunk->data[2] == 'A'))
    {
        return FMOD_ERR_FORMAT;
    }
    position += size;
    while (!(chunk->header[0] == 'm' && chunk->header[1] == 'd' && chunk->header[2] == 'a' && chunk->header[3] == 't'))
    {
        delete(chunk->data);

//      r = state->functions->seek(state, position, FMOD_CODEC_SEEK_METHOD_SET);
        r = state->functions->read(state, chunk->size, 4, &readBytes);
        r = state->functions->read(state, chunk->header, 4, &readBytes);
        if (chunk->size[0] & 0x00 && chunk->size[1] & 0x00 && chunk->size[2] & 0x00 && chunk->size[3] & 0x01)
        {
            r = state->functions->read(state, bytes, 8, &readBytes);
            size = _get_size_64(bytes);
            chunk->data = new BYTE[size - 16];
            memset(chunk->data, 0, size - 16);
            r = state->functions->read(state, chunk->data, size - 16, &readBytes);
            size = size - 16;
        }
        else {
            size = _get_size(chunk->size);
            chunk->data = new BYTE[size - 8];
            memset(chunk->data, 0, size - 8);
            r = state->functions->read(state, chunk->data, size - 8, &readBytes);
            size = size - 8;
        }

        position += size;
        if (r == FMOD_ERR_FILE_EOF)
            break;
    } 

    if (r != FMOD_OK)
    {
        delete(chunk->data);
        return FMOD_ERR_FILE_EOF;
    }

    info* x = new info;
    if (!x)
    {
        delete(chunk->data);
        return FMOD_ERR_INTERNAL;
    }

    memset(x, 0, sizeof(info));

    x->buflen = size;
    memcpy(x->buffer, chunk->data, x->buflen);

    x->initbytes = 0;
    if (get_AAC_format(x) == -1)
    {
        delete(x->buffer);
        return FMOD_ERR_FILE_BAD;
    }

    if (!(x->aac = NeAACDecOpen()))
    {
        delete(x->buffer);
        return FMOD_ERR_INTERNAL;
    }

    if (x->initbytes < 0 || x->initbytes > BUFFER_SIZE)
    {
        delete(x->buffer);
        return FMOD_ERR_INTERNAL;
    }

    memmove(x->buffer, x->buffer + x->initbytes, size - x->initbytes);
    x->buflen -= x->initbytes;

    NeAACDecConfigurationPtr config;
    /* Set configuration */
    config = NeAACDecGetCurrentConfiguration(x->aac);
    config->outputFormat = FAAD_FMT_16BIT;
    config->downMatrix = 1;
    config->dontUpSampleImplicitSBR = 1;
    NeAACDecSetConfiguration(x->aac, config);

    NeAACDecFrameInfo info;

    if (NeAACDecInit(x->aac, x->buffer, x->buflen, &x->sample_rates, &x->channels) != 0)
    {
        delete(x->buffer);
        return FMOD_ERR_INTERNAL;
    }

    aacwaveformat.channels = x->channels;
    aacwaveformat.format = FMOD_SOUND_FORMAT_PCM16;
    aacwaveformat.frequency = x->sample_rates;
    aacwaveformat.pcmblocksize = aacwaveformat.channels * 2;      //    2 = 16bit pcm 
    aacwaveformat.lengthpcm = 0xffffffff;// bytes converted to PCM samples ;

    state->numsubsounds = 0; //number of 'subsounds' in this sound.  For most codecs this is 0, only multi sound codecs such as FSB or CDDA have subsounds. 
    state->waveformat = &aacwaveformat;

    state->plugindata = x;

    return FMOD_OK;
};

// closeコールバック関数は、デコードしたデータを解放するために使用されます。
FMOD_RESULT F_CALLBACK myCodec_close(FMOD_CODEC_STATE* state){
    // デコードしたデータを解放する
    if (state->plugindata != nullptr)
    {
        info* x = (info*)state->plugindata;
        if (x->aac)
            NeAACDecClose(x->aac);
        delete(x);
    }

    return FMOD_OK;
};

// readコールバック関数は、デコードしたデータを返すために使用されます。
FMOD_RESULT F_CALLBACK myCodec_read(FMOD_CODEC_STATE* state, void* buffer, unsigned int sizebytes, unsigned int* bytesread)
{
    // デコードしたデータをbufferにコピーする
	memset(buffer, 0, sizebytes);

	if(sizebytes < 4096 * 2) {
		*bytesread = sizebytes;
		return FMOD_OK;
	}

    info* x = (info*)state->plugindata;
	if(!x || !bytesread)
		return FMOD_ERR_INTERNAL;

	void* buf = NULL;
	unsigned int donelen = 0;
    unsigned int wav_data_len = 0;
    unsigned int aac_buflen = x->buflen;
    unsigned int framelen;
    NeAACDecFrameInfo info;

    while (donelen < aac_buflen)
    {
        framelen = _get_frame_length((char*)(x->buffer + donelen));
        if (donelen + framelen > aac_buflen)
        {
            break;
        }


        buf = NeAACDecDecode(x->aac, &info, x->buffer + donelen, framelen);
        if (buf && info.error == 0) 
        {
            switch (info.samplerate)
            {
                case 22050:
                {
                    //22050Hz
                    //src: 1024 samples, 2048 bytes
                    //dst: 2048 samples, 4096 bytes
                    short* ori = (short*)buf;
                    short tmpbuf[2048];
                    unsigned int tmplen = info.samples * 16 / 8 * 2;
                    for (int i = 0, j = 0; i < info.samples; i += 2)
                    {
                        tmpbuf[j++] = ori[i];
                        tmpbuf[j++] = ori[i + 1];
                        tmpbuf[j++] = ori[i];
                        tmpbuf[j++] = ori[i + 1];
                    }
                    memmove((unsigned char*)buffer + wav_data_len, tmpbuf, tmplen);
                    wav_data_len += tmplen;
                    break;
                }
                case 44100:
                {
                    //44100Hz
                    //src: 2048 samples, 4096 bytes
                    //dst: 2048 samples, 4096 bytes
                    unsigned int tmplen = info.samples * 16 / 8;
                    memmove((unsigned char*)buffer + wav_data_len, buf, tmplen);
                    wav_data_len += tmplen;
                    break;
                }
            }
        } else {
            *bytesread = 0;
            return FMOD_ERR_FILE_BAD;
        }
        donelen += framelen;

    }

    *bytesread = wav_data_len;
	return FMOD_OK;
};

// getlengthコールバック関数は、オーディオファイルの長さを返すために使用されます。
FMOD_RESULT F_CALLBACK myCodec_getlength(FMOD_CODEC_STATE* state, unsigned int* length, FMOD_TIMEUNIT lengthtype)
{
    // オーディオファイルの長さを計算する
//    *length = 0;
    return FMOD_OK;
};

// setpositionコールバック関数は、再生位置を設定するために使用されます。
FMOD_RESULT F_CALLBACK myCodec_setposition(FMOD_CODEC_STATE* state, int subsound, unsigned int position, FMOD_TIMEUNIT postype)
{
    // 再生位置を設定する
    return FMOD_OK;
};

// getpositionコールバック関数は、再生位置を取得するために使用されます。
FMOD_RESULT F_CALLBACK myCodec_getposition(FMOD_CODEC_STATE* state, unsigned int* position, FMOD_TIMEUNIT postype)
{
    // 再生位置を設定する
    return FMOD_OK;
};

// soundcreatedコールバック関数は、サウンドが作成されたときに呼び出されます。
FMOD_RESULT F_CALLBACK myCodec_soundcreated(FMOD_CODEC_STATE* state, int subsound, FMOD_SOUND* sound)
{
    // サウンドが作成されたときの処理
    return FMOD_OK;
};

FMOD_RESULT F_CALLBACK myCodec_getWaveFormat(FMOD_CODEC_STATE* state, int index, FMOD_CODEC_WAVEFORMAT* waveformat)
{
    // オーディオファイルの形式情報を取得する
    return FMOD_OK;
};


/*
    Codec structures

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

FMOD_CODEC_DESCRIPTION myCodec = {
    FMOD_CODEC_PLUGIN_VERSION,      // バージョン番号
    "FMOD MP4/AAC Codec",           // コーデックの名前
    0x00010000,                     // ドライバーのバージョン番号
    0,                              // Default As Stream
    FMOD_TIMEUNIT_MS | FMOD_TIMEUNIT_PCM, // Timeunit
    &myCodec_open,                   // openコールバック
    &myCodec_close,                  // closeコールバック
    &myCodec_read,                   // readコールバック
    &myCodec_getlength,              // getlengthコールバック
    &myCodec_setposition,            // setpositionコールバック
    &myCodec_getposition,            // setpositionコールバック
    &myCodec_soundcreated,           // soundcreatedコールバック
    &myCodec_getWaveFormat           // getWaveFormatExコールバック
};

/*
FMODGetCodecDescription is mandatory for every fmod plugin.  This is the symbol the registerplugin function searches for.
Must be declared with F_API to make it export as stdcall.
MUST BE EXTERN'ED AS C!  C++ functions will be mangled incorrectly and not load in fmod.
*/


#ifdef __cplusplus
extern "C" {
#endif
    __declspec(dllexport)  FMOD_CODEC_DESCRIPTION* F_API FMODGetCodecDescription()
    {
        return &myCodec;
    }

#ifdef __cplusplus
}
#endif