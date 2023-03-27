#pragma comment(lib, "libfaad.lib")

#include <iterator>
#include <vector>
#include <algorithm>
#include "neaacdec.h"
#include "fmod.h"

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

    std::uint32_t lengthpcm;
    std::uint32_t pcmblocks;
    std::uint64_t position;
} info;

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

// openコールバック関数は、ファイルからデータを読み込んでデコードするために使用されます。
FMOD_RESULT F_CALLBACK myCodec_open(FMOD_CODEC_STATE* codec, FMOD_MODE usermode, FMOD_CREATESOUNDEXINFO* userexinfo)
{
    // ファイルからデータを読み込んで、state->plugindataに保存する

    if (!codec)
        return FMOD_ERR_INTERNAL;

    std::uint32_t totalSize = 0;
    codec->functions->size(codec, &totalSize);

    // 無いと思うけど読み込んだファイルがサイズ0以下の場合はEOF扱い
    if (totalSize <= 0)
        return FMOD_ERR_FILE_EOF;

    std::uint64_t totalRead = 0;    // 累計リードサイズ
    std::uint32_t readBytes = 0;    // リードサイズ

    // MP4 HEADER
    MP4HEADER* chunk = new MP4HEADER;

    FMOD_RESULT r;
    std::uint64_t size = 0;         // データ領域のサイズ
    std::byte bytes[8];             // EXTEND SIZE

    // EXTEND SIZEフラグ
    std::byte extend[4] = { std::byte(0x00), std::byte(0x00), std::byte(0x00), std::byte(0x01) };

    // 先頭ブロック(8バイト)の読み込み
    r = codec->functions->read(codec, chunk->size, 4, &readBytes);
    r = codec->functions->read(codec, chunk->header, 4, &readBytes);

    // =========================================================
    // 先頭ブロックがftyp:M4Aの場合のみ、このコーデックは有効
    // =========================================================
    // ftypブロックの検出
    if (std::memcmp(chunk->header, "ftyp", 4) != 0)
        return FMOD_ERR_FORMAT; // フォーマットエラー

    // データ領域のサイズ取得
    size = _get_size(chunk->size);
    // データ領域確保(ヘッダブロックの8バイトは減算)
    chunk->data.resize(size - 8);

    // ftype:[M4A]　データ領域からファイルタイプの取得
    r = codec->functions->read(codec, chunk->data.data(), size - 8, &readBytes);
    if (std::memcmp(chunk->data.data(), "M4A ", 4) != 0
    &&  std::memcmp(chunk->data.data(), "mp42", 4) != 0)
        return FMOD_ERR_FORMAT; // フォーマットエラー

    // mdatブロックまでループ
    while (std::memcmp(chunk->header, "mdat", 4) != 0)
    {
        // ヘッダブロック8バイト読み込み
        r = codec->functions->read(codec, chunk->size, 4, &readBytes);
        totalRead += readBytes;
        r = codec->functions->read(codec, chunk->header, 4, &readBytes);
        totalRead += readBytes;

        // サイズ領域がEXTEND SIZEフラグの場合
        if (std::memcmp(chunk->size, extend, 4) == 0)
        {
            // さらに8バイト読み込み
            r = codec->functions->read(codec, bytes, 8, &readBytes);
            totalRead += readBytes;
            // 8バイトがデータサイズとなる（ヘッダ16バイト分は減算）
            size = _get_size_64(bytes) - 16;
        }
        else {
            // 通常のデータサイズ取得
            size = _get_size(chunk->size) - 8;
        }
        // データ領域確保
        chunk->data.resize(size);
        // データ部読み込み
        r = codec->functions->read(codec, chunk->data.data(), size, &readBytes);
        totalRead += readBytes;

        // 累計読み込みサイズがファイルサイズ以上の場合はループ脱出
        if (totalRead >= totalSize)
            break;
    }

    // 最後に読み込んだヘッダがmdatでは無かった場合は見つからなかったとしてエラー
    if (std::memcmp(chunk->header, "mdat", 4) != 0)
        return FMOD_ERR_FORMAT; // フォーマットエラー

    // API連携データ
    info* x = new info;
    if (!x)
        return FMOD_ERR_INTERNAL;

    memset(x, 0, sizeof(info));

    // 最後のmdatのデータサイズ
    x->bufferlen = readBytes;
    x->buffer.clear();
    x->buffer.resize(x->bufferlen);
    // データ部をAPI連携データへ
    std::memcpy(x->buffer.data(), chunk->data.data(), x->bufferlen);

    // FAAD2デコーダオープン
    if (!(x->aac = NeAACDecOpen()))
        return FMOD_ERR_INTERNAL;

    // FAAD2デコーダ初期化
    if (NeAACDecInit(x->aac, reinterpret_cast<unsigned char*>(x->buffer.data()), x->bufferlen, reinterpret_cast<unsigned long*>(&x->sample_rates), reinterpret_cast<unsigned char*>(&x->channels)) != 0)
    {
        // 失敗した場合はクローズしてエラー
        if (x->aac)
            NeAACDecClose(x->aac);
        return FMOD_ERR_INTERNAL;
    }

    // FAAD2コンフィグ（この辺のパラメータは理解できてない）
    NeAACDecConfigurationPtr config;
    /* Set configuration */
    config = NeAACDecGetCurrentConfiguration(x->aac);
    config->outputFormat = FAAD_FMT_16BIT;
    config->defSampleRate = 44100;
    NeAACDecSetConfiguration(x->aac, config);


    void* buf = NULL;               // デコードデータ
    std::uint64_t position = 0;     // データ部の現在位置
    std::uint64_t read = 0;         // デコード後のデータサイズ
    std::vector<std::byte> decoded; // 全デコードデータ
    decoded.clear();

    NeAACDecFrameInfo info;

    // データ部の現在位置がデータサイズ未満の間はループ
    while (position < x->bufferlen)
    {
        // データ部の現在位置からデコード
        buf = NeAACDecDecode(x->aac, &info, (unsigned char*)&x->buffer[position], x->bufferlen - position);

        // エラーの場合はファイル破損
        if (info.error != 0)
        {
            x->bufferlen = 0;
            return FMOD_ERR_FILE_BAD;
        }

        // bytesconsumedは恐らくデコードに成功したサイズ
        if (info.bytesconsumed > x->bufferlen)
        {
            x->bufferlen = 0;
        }
        else
        {
            // samplesが何を指してるか分からんがとりあえずゼロじゃないはず
            if (info.samples != 0)
            {
                // デコードデータが無い場合はエラー
                if (!buf)
                    return FMOD_ERR_INTERNAL;

                // デコードに成功したサイズは勿論ゼロより大きいはず
                if (info.bytesconsumed > 0)
                {
                    // 全デコードサイズの領域確保（追記型）
                    // bytesconsumed分のデコード後のサイズはinfo.samples * 2になるらしい（ホンマか？）
                    decoded.resize(decoded.size() + info.samples * 2);
                    // とりあえず全デコードデータへ今回のデコード分を追記
                    std::memcpy(&decoded[read], buf, info.samples * 2);

                    // データ部の位置をシーク
                    position += info.bytesconsumed;
                    // 全デコード後のサイズを加算
                    read += info.samples * 2;
                }
            }
        }
    }
    // デコード後のサイズ
    x->bufferlen = read;
    // これは曲の長さを取得しているが、何故1/4なのか分からん
    x->lengthpcm = read / 4;
    // データ部の領域を使って全デコードデータをAPI連携させる為領域確保しなおし
    x->buffer.clear();
    x->buffer.resize(read);
    // デコードデータのコピー
    std::memcpy(x->buffer.data(), decoded.data(), read);

    // おまじない
    codec->numsubsounds = 0; //number of 'subsounds' in this sound.  For most codecs this is 0, only multi sound codecs such as FSB or CDDA have subsounds. 

    // API連携
    codec->plugindata = x;

    return FMOD_OK;
};

// closeコールバック関数は、デコードしたデータを解放するために使用されます。
FMOD_RESULT F_CALLBACK myCodec_close(FMOD_CODEC_STATE* codec){
    // デコードしたデータを解放する
    // デコーダはクローズ忘れずに
    if (codec->plugindata != nullptr)
    {
        info* x = (info*)codec->plugindata;
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

    // bufferは恐らくFMODが再生出来る領域
    // sizebytesはFMOD側が確保してくれてる領域だと思う
    // 何故か4倍した方が良かった
	memset(buffer, 0, sizebytes * 4);

    // API連携データの取得
    info* x = (info*)codec->plugindata;
    if (!x || !bytesread)
        return FMOD_ERR_INTERNAL;

    // 全デコードデータからFMODへ流す。何故か初期領域の4倍の量を渡したらうまくいった
    std::memcpy(buffer, &x->buffer[x->position], sizebytes * 4);
    // 再生位置の加算（もちろん4倍）
    x->position += sizebytes * 4;
    // 今回読み込んだデータサイズ（もちろん4倍　謎）
    *bytesread = sizebytes * 4;
    return FMOD_OK;
};

// getlengthコールバック関数は、オーディオファイルの長さを返すために使用されます。
FMOD_RESULT F_CALLBACK myCodec_getlength(FMOD_CODEC_STATE* codec, unsigned int* length, FMOD_TIMEUNIT lengthtype)
{
    // 使わなかった
    return FMOD_OK;
};

// setpositionコールバック関数は、再生位置を設定するために使用されます。
FMOD_RESULT F_CALLBACK myCodec_setposition(FMOD_CODEC_STATE* codec, int subsound, unsigned int position, FMOD_TIMEUNIT postype)
{
    info* x = (info*)codec->plugindata;
    if (!x)
        return FMOD_ERR_INTERNAL;

    // 再生位置を設定する
    x->position = position;
    return FMOD_OK;
};

// getpositionコールバック関数は、再生位置を取得するために使用されます。
FMOD_RESULT F_CALLBACK myCodec_getposition(FMOD_CODEC_STATE* codec, unsigned int* position, FMOD_TIMEUNIT postype)
{
    info* x = (info*)codec->plugindata;
    if (!x)
        return FMOD_ERR_INTERNAL;

    // 再生位置を設定する
    *position = x->position;
    return FMOD_OK;
};

// soundcreatedコールバック関数は、サウンドが作成されたときに呼び出されます。
FMOD_RESULT F_CALLBACK myCodec_soundcreated(FMOD_CODEC_STATE* codec, int subsound, FMOD_SOUND* sound)
{
    // サウンドが作成されたときの処理
    return FMOD_OK;
};

FMOD_RESULT F_CALLBACK myCodec_getWaveFormat(FMOD_CODEC_STATE* codec, int index, FMOD_CODEC_WAVEFORMAT* waveformat)
{
    info* x = (info*)codec->plugindata;
    if (!x)
        return FMOD_ERR_INTERNAL;

    // PCMデータフォーマットの設定だと思う
    // 読み込んだAACデータを元に可変にしてあげるべきだろうけどやり方分からん
    waveformat->channels = static_cast<int>(x->channels);
    waveformat->format = FMOD_SOUND_FORMAT_PCM16;
    waveformat->mode = FMOD_DEFAULT;
    waveformat->frequency = x->sample_rates;
    // この値ヘタにいじるとアクセス違反発生する
//  waveformat->pcmblocksize = static_cast<int>(x->channels) * 2;      //    2 = 16bit pcm 
//  waveformat->pcmblocksize = x->pcmblocks;
    // 曲の長さ
    waveformat->lengthpcm = x->lengthpcm;// bytes converted to PCM samples ;

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

// コーデックの情報
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