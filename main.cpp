#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>
#include <memory>
#include <algorithm>
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

typedef struct 
{
    std::byte size[4];
    std::byte header[4];
    std::vector<std::byte> data;
} MP4HEADER;

typedef struct 
{
    std::uint64_t sample_rates;
    std::byte channels;
    NeAACDecHandle aac;
    std::vector<std::byte> buffer;
    std::uint64_t bufferlen;
    std::uint32_t initbytes;

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

std::uint32_t _get_size(const std::byte* size)
{
    std::uint32_t x = 0;

    for (size_t i = 0; i < sizeof(std::uint32_t); i++)
    {
        const std::uint8_t bit_shifts = (sizeof(std::uint32_t) - 1 - i) * 8;
        x |= (std::uint32_t)size[i] << bit_shifts;
    }
    return x;
}

std::uint64_t _get_size_64(const std::byte* size)
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
        if (x->buffer.at(a) == std::byte(0xFF) && (x->buffer.at(a + 1) & std::byte(0xF6)) == std::byte(0xF0)
            && static_cast<std::uint64_t>((x->buffer.at(a + 2) & std::byte(0x3C)) >> 2) < 12) { // ADTS header syncword
            x->initbytes += a;
            return 0;
        }
    } while (++a < x->bufferlen - 4);
    return -1;
}

// openコールバック関数は、ファイルからデータを読み込んでデコードするために使用されます。
FMOD_RESULT F_CALLBACK myCodec_open(FMOD_CODEC_STATE* state, FMOD_MODE usermode, FMOD_CREATESOUNDEXINFO* userexinfo)
{
    // ファイルからデータを読み込んで、state->plugindataに保存する

    if (!state) 
        return FMOD_ERR_INTERNAL;

    std::uint32_t totalSize = 0;
    state->functions->size(state, &totalSize);
    if (totalSize <= 0)
        return FMOD_ERR_FILE_EOF;

    std::uint64_t totalRead = 0;
    std::uint32_t readBytes = 0;

    // MP4 HEADER
    MP4HEADER* chunk = new MP4HEADER;

    FMOD_RESULT r;
    std::uint64_t size = 0;
    std::byte bytes[8];
    std::byte extend[4] = { std::byte(0x00), std::byte(0x00), std::byte(0x00), std::byte(0x01) };

    r = state->functions->read(state, chunk->size, 4, &readBytes);
    r = state->functions->read(state, chunk->header, 4, &readBytes);

    // ftypブックの検出
    if (std::memcmp(chunk->header, "ftyp", 4) != 0)
        return FMOD_ERR_FORMAT;

    size = _get_size(chunk->size);
    chunk->data.resize(size - 8);

    // Get File Type: M4A以外は対象外
    r = state->functions->read(state, chunk->data.data(), size - 8, &readBytes);
    if (std::memcmp(chunk->data.data(), "M4A ", 4) != 0)
        return FMOD_ERR_FORMAT;

    while (std::memcmp(chunk->header, "mdat", 4) != 0)
    {
        r = state->functions->read(state, chunk->size, 4, &readBytes);
        totalRead += readBytes;
        r = state->functions->read(state, chunk->header, 4, &readBytes);
        totalRead += readBytes;

        // EXTEND SIZE
        if (std::memcmp(chunk->size, extend, 4) == 0)
        {
            r = state->functions->read(state, bytes, 8, &readBytes);
            totalRead += readBytes;
            size = _get_size_64(bytes) - 16;
        }
        else {
            size = _get_size(chunk->size) - 8;
        }
        chunk->data.resize(size);
        r = state->functions->read(state, chunk->data.data(), size, &readBytes);
        totalRead += readBytes;

        if (totalRead >= totalSize)
            break;
    }

    if (std::memcmp(chunk->header, "mdat", 4) != 0)
        return FMOD_ERR_FORMAT;

    info* x = new info;
    if (!x)
        return FMOD_ERR_INTERNAL;

    memset(x, 0, sizeof(info));

    // 最後のmdatのデータ領域
    x->bufferlen = readBytes;
    x->buffer.clear();
    x->buffer.resize(x->bufferlen);
    std::memcpy(x->buffer.data(), chunk->data.data(), x->bufferlen);
    x->initbytes = 0;
//    if (get_AAC_format(x) == -1)
//        return FMOD_ERR_FILE_BAD;

    if (!(x->aac = NeAACDecOpen()))
        return FMOD_ERR_INTERNAL;

//    if (x->initbytes < 0 || x->initbytes > x->bufferlen)
//        return FMOD_ERR_INTERNAL;

//    x->bufferlen -= x->initbytes;

    if (NeAACDecInit(x->aac, reinterpret_cast<unsigned char*>(x->buffer.data()), x->bufferlen, reinterpret_cast<unsigned long*>(&x->sample_rates), reinterpret_cast<unsigned char*>(&x->channels)) != 0)
    {
        if (x->aac)
            NeAACDecClose(x->aac);
        return FMOD_ERR_INTERNAL;
    }

    NeAACDecConfigurationPtr config;
    /* Set configuration */
    config = NeAACDecGetCurrentConfiguration(x->aac);
    config->outputFormat = FAAD_FMT_16BIT;
    config->downMatrix = 1;
    config->dontUpSampleImplicitSBR = 1;
    NeAACDecSetConfiguration(x->aac, config);

    NeAACDecFrameInfo info;

    aacwaveformat.channels = static_cast<int>(x->channels);
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
FMOD_RESULT F_CALLBACK myCodec_read(FMOD_CODEC_STATE* codec, void* buffer, unsigned int sizebytes, unsigned int* bytesread)
{
    // デコードしたデータをbufferにコピーする
	memset(buffer, 0, sizebytes);

    if (sizebytes < 4096 * 2) {
        *bytesread = sizebytes;
        return FMOD_OK;
    }

    info* x = (info*)codec->plugindata;
    if (!x || !bytesread)
        return FMOD_ERR_INTERNAL;

    void* buf = NULL;
    unsigned int buflen = 0;
    unsigned int r;

    NeAACDecFrameInfo info;
    MP4HEADER* chunk = new MP4HEADER();
    bool eof = false;
    while (buflen < sizebytes || eof) {
        do {
            r = 0;
            FMOD_RESULT res;
            res = codec->functions->read(codec, chunk->data.data(), BUFFER_SIZE - x->bufferlen, &r);
            if (res == FMOD_ERR_FILE_EOF)
                eof = true;
            else if (res != FMOD_OK)
                return FMOD_ERR_INTERNAL;

            x->bufferlen += r;
            buf = NeAACDecDecode(x->aac, &info, reinterpret_cast<unsigned char*>(x->buffer.data()), x->bufferlen);
            if (info.error != 0) {
                *bytesread = 0;
                return FMOD_ERR_FILE_BAD;
            }
            if (info.bytesconsumed > x->bufferlen) {
                x->bufferlen = 0;
            }
            else {
                x->bufferlen -= info.bytesconsumed;
                std::memmove(x->buffer.data(), x->buffer.data() + info.bytesconsumed, x->bufferlen); // shift remaining data to start of buffer
            }
        } while (!info.samples || eof);
        if (info.samples != 0) {
            if (!buf)
                return FMOD_ERR_INTERNAL;
            memcpy((unsigned char*)buffer + buflen, buf, info.samples * 2);
            buflen += info.samples * 2;
        }
    }
    *bytesread = buflen;
    if (eof) return FMOD_ERR_FILE_EOF;
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
    FMOD_TIMEUNIT_PCMBYTES, // Timeunit
    &myCodec_open,                   // openコールバック
    &myCodec_close,                  // closeコールバック
    &myCodec_read,                   // readコールバック
    &myCodec_getlength,              // getlengthコールバック
    &myCodec_setposition,            // setpositionコールバック
    &myCodec_getposition,            // getpositionコールバック
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