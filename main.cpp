#include "neaacdec.h"
#include "fmod.h"
#include <string.h>
#include <stdio.h>
//#include "fmod_errors.h"

#define MAX_CHANNELS 8
#define BUFFER_SIZE (FAAD_MIN_STREAMSIZE * MAX_CHANNELS)
#define MAX_PERCENTS 384

typedef unsigned long       DWORD;
typedef int                 BOOL;
typedef unsigned char       BYTE;
typedef unsigned short      WORD;

typedef struct {
    char size[4];
    char header[4];
    BYTE* data;

} MP4HEADER;
typedef struct {
    unsigned int initbytes;
    DWORD sr;
    BYTE nch;
    NeAACDecHandle neaac;
    unsigned char fbuf[BUFFER_SIZE];
    DWORD fbuflen;
} info;

static FMOD_CODEC_WAVEFORMAT    aacwaveformat;
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
    r = state->functions->read(state, chunk->size, 4, &readBytes);
    r = state->functions->read(state, chunk->header, 4, &readBytes);
    // Get File Type
    r = state->functions->read(state, chunk->data, *chunk->size - 8, &readBytes);

    if (!(chunk->header[0] == 'f' && chunk->header[1] == 't' && chunk->header[2] == 'y' && chunk->header[3] == 'p'))
    {
        return FMOD_ERR_FORMAT;
    }

    if (!(chunk->data[0] == 'M' && chunk->data[1] == '4' && chunk->data[2] == 'A'))
    {
        return FMOD_ERR_FORMAT;
    }

    while (!(chunk->header[0] == 'm' && chunk->header[1] == 'd' && chunk->header[2] == 'a' && chunk->header[3] == 't'))
    {
        r = state->functions->read(state, chunk->size, 4, &readBytes);
        r = state->functions->read(state, chunk->header, 4, &readBytes);
        r = state->functions->read(state, chunk->data, *chunk->size - 8, &readBytes);
    } 

    if (r != FMOD_OK)
        return FMOD_ERR_FILE_EOF;

    info* x = new info;
    if (!x) 
        return FMOD_ERR_INTERNAL;

    memset(x, 0, sizeof(info));

    x->initbytes = 0;
    if (!(x->neaac = NeAACDecOpen()))
        return FMOD_ERR_INTERNAL;

    if (x->initbytes < 0 || x->initbytes > BUFFER_SIZE)
        return FMOD_ERR_INTERNAL;

    NeAACDecConfigurationPtr config;
    /* Set configuration */
    config = NeAACDecGetCurrentConfiguration(x->neaac);
    config->outputFormat = FAAD_FMT_16BIT;
    config->downMatrix = 1;
    config->dontUpSampleImplicitSBR = 1;
    NeAACDecSetConfiguration(x->neaac, config);

    NeAACDecFrameInfo info;
    if (NeAACDecInit2(x->neaac, chunk->data, *chunk->size - 8, &x->sr, &x->nch) != 0)
        return FMOD_ERR_INTERNAL;

    void* samplebuffer = NeAACDecDecode(x->neaac, &info, chunk->data, *chunk->size - 8);
    if (info.error == 0 && info.samplerate > 0)
    {
        FILE *fp = fopen("TEST.wav", "w");
        fwrite(samplebuffer, info.samples, 1, fp);
        fclose(fp);
        state->plugindata = samplebuffer;
        NeAACDecClose(x->neaac);
        return FMOD_OK;
    }

    return FMOD_ERR_INTERNAL;

    /*
    unsigned int samplerate_ = ((adts[1] & 0x07) << 1) | (adts[2] >> 7);
    unsigned int channels_ = (adts[2] >> 3) & 0x0f;
    unsigned int samplesize_;

    // Determine sample size
    switch (adts[2] & 0x07)
    {
        case 0: // 960 samples per frame
            samplesize_ = 960 * channels_ * 2;
            break;
        case 1: // 1024 samples per frame
            samplesize_ = 1024 * channels_ * 2;
            break;
        case 2: // 480 samples per frame
            samplesize_ = 480 * channels_ * 2;
            break;
        default:
            return FMOD_ERR_FORMAT;
    }

    // Allocate buffer for decoded PCM data
    unsigned char *buffer_ = new unsigned char[samplesize_];

    // Return file size and handle
    unsigned int filesize = length_ - sizeof(adts);

    aacwaveformat.channels = channels_;
    aacwaveformat.format = FMOD_SOUND_FORMAT_PCM16;
    aacwaveformat.frequency = samplerate_;
    aacwaveformat.pcmblocksize = samplesize_;//aacwaveformat.channels * 2;          2 = 16bit pcm 
    aacwaveformat.lengthpcm = filesize;// codec->filesize;// / aacwaveformat.blockalign;   bytes converted to PCM samples ;

    state->numsubsounds = 0;                     number of 'subsounds' in this sound.  For most codecs this is 0, only multi sound codecs such as FSB or CDDA have subsounds. 
    state->waveformat = &aacwaveformat;
*/
};

// closeコールバック関数は、デコードしたデータを解放するために使用されます。
FMOD_RESULT F_CALLBACK myCodec_close(FMOD_CODEC_STATE* state){
    // デコードしたデータを解放する
    return FMOD_OK;
};

// readコールバック関数は、デコードしたデータを返すために使用されます。
FMOD_RESULT F_CALLBACK myCodec_read(FMOD_CODEC_STATE* state, void* buffer, unsigned int sizebytes, unsigned int* bytesread)
{
    // デコードしたデータをbufferにコピーする
    *bytesread = sizebytes;
    return FMOD_OK;
};

// getlengthコールバック関数は、オーディオファイルの長さを返すために使用されます。
FMOD_RESULT F_CALLBACK myCodec_getlength(FMOD_CODEC_STATE* state, unsigned int* length, FMOD_TIMEUNIT lengthtype)
{
    // オーディオファイルの長さを計算する
    *length = 0;
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