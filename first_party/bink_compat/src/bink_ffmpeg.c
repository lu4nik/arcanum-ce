// Local tweak: Bink playback through FFmpeg for platforms without binkw32.dll.
//
// Implements the small part of the Bink API the game uses (open, wait, decode,
// copy to a 32-bit buffer, next frame, close) on top of libavformat/libavcodec
// (a minimal static build with only the Bink demuxer and decoders). Audio is
// decoded alongside the video and handed to the game's own Bink sound system
// (SDL_mixer) as 16-bit PCM, so it follows the game's volume.

#include "bink_ffmpeg.h"

#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

typedef struct BinkFF {
    BINK pub; // must stay first: HBINK points here
    AVFormatContext* fmt;
    AVCodecContext* vdec;
    AVCodecContext* adec;
    int vidx;
    int aidx;
    AVFrame* vframe;
    AVFrame* aframe;
    AVPacket* pkt;
    double fps;
    Uint64 start_ns;
    bool started;
    bool eof;
    BINKSND snd;
    bool snd_ok;
} BinkFF;

static BINKSNDOPEN ff_snd_open;
static unsigned ff_sound_track;

static void push_audio(BinkFF* b, const AVFrame* f)
{
    if (!b->snd_ok || f->nb_samples <= 0) {
        return;
    }

    int ch = f->ch_layout.nb_channels;
    int n = f->nb_samples;
    size_t bytes = (size_t)n * ch * 2;
    int16_t* pcm = (int16_t*)SDL_malloc(bytes);
    if (pcm == NULL) {
        return;
    }

    for (int i = 0; i < n; i++) {
        for (int c = 0; c < ch; c++) {
            float v = 0.0f;
            switch (f->format) {
            case AV_SAMPLE_FMT_FLTP:
                v = ((const float*)f->extended_data[c])[i];
                break;
            case AV_SAMPLE_FMT_FLT:
                v = ((const float*)f->extended_data[0])[i * ch + c];
                break;
            case AV_SAMPLE_FMT_S16P:
                v = ((const int16_t*)f->extended_data[c])[i] / 32768.0f;
                break;
            case AV_SAMPLE_FMT_S16:
                v = ((const int16_t*)f->extended_data[0])[i * ch + c] / 32768.0f;
                break;
            default:
                break;
            }
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            pcm[i * ch + c] = (int16_t)(v * 32767.0f);
        }
    }

    // The game's sound system hands out a fixed-size buffer per Lock.
    const u8* p = (const u8*)pcm;
    size_t left = bytes;
    while (left > 0) {
        u8* addr = NULL;
        u32 len = 0;
        if (!b->snd.Lock(&b->snd, &addr, &len) || addr == NULL || len == 0) {
            break;
        }
        u32 chunk = (u32)(left < len ? left : len);
        chunk -= chunk % (u32)(ch * 2);
        if (chunk == 0) {
            break;
        }
        memcpy(addr, p, chunk);
        b->snd.Unlock(&b->snd, chunk);
        p += chunk;
        left -= chunk;
    }

    SDL_free(pcm);
}

static void drain_audio(BinkFF* b)
{
    while (avcodec_receive_frame(b->adec, b->aframe) == 0) {
        push_audio(b, b->aframe);
        av_frame_unref(b->aframe);
    }
}

static void ff_close(HBINK bnk)
{
    BinkFF* b = (BinkFF*)bnk;
    if (b == NULL) {
        return;
    }
    if (b->snd_ok && b->snd.Close != NULL) {
        b->snd.Close(&b->snd);
    }
    av_packet_free(&b->pkt);
    av_frame_free(&b->vframe);
    av_frame_free(&b->aframe);
    avcodec_free_context(&b->vdec);
    avcodec_free_context(&b->adec);
    avformat_close_input(&b->fmt);
    free(b);
}

static AVCodecContext* open_decoder(AVStream* st)
{
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (codec == NULL) {
        return NULL;
    }
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (ctx == NULL) {
        return NULL;
    }
    if (avcodec_parameters_to_context(ctx, st->codecpar) < 0 || avcodec_open2(ctx, codec, NULL) < 0) {
        avcodec_free_context(&ctx);
        return NULL;
    }
    return ctx;
}

static HBINK ff_open(const char* name, unsigned flags)
{
    BinkFF* b = (BinkFF*)calloc(1, sizeof(*b));
    if (b == NULL) {
        return NULL;
    }
    b->vidx = -1;
    b->aidx = -1;

    if (avformat_open_input(&b->fmt, name, NULL, NULL) < 0
        || avformat_find_stream_info(b->fmt, NULL) < 0) {
        SDL_Log("Bink (FFmpeg): cannot open %s", name);
        ff_close((HBINK)b);
        return NULL;
    }

    for (unsigned i = 0; i < b->fmt->nb_streams; i++) {
        AVStream* st = b->fmt->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && b->vidx < 0) {
            b->vidx = (int)i;
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            bool wanted = (flags & BINKSNDTRACK) != 0 && (unsigned)st->id == ff_sound_track;
            if (b->aidx < 0 || wanted) {
                b->aidx = (int)i;
            }
        }
    }

    if (b->vidx < 0 || (b->vdec = open_decoder(b->fmt->streams[b->vidx])) == NULL) {
        SDL_Log("Bink (FFmpeg): no video in %s", name);
        ff_close((HBINK)b);
        return NULL;
    }
    if (b->aidx >= 0) {
        b->adec = open_decoder(b->fmt->streams[b->aidx]);
        if (b->adec == NULL) {
            b->aidx = -1;
        }
    }

    AVStream* vs = b->fmt->streams[b->vidx];
    double fps = av_q2d(vs->avg_frame_rate);
    if (fps <= 0.0) fps = av_q2d(vs->r_frame_rate);
    if (fps <= 0.0 && vs->time_base.num > 0) fps = 1.0 / av_q2d(vs->time_base);
    if (fps <= 0.0) fps = 15.0;
    b->fps = fps;

    b->pub.Width = (unsigned)b->vdec->width;
    b->pub.Height = (unsigned)b->vdec->height;
    b->pub.Frames = vs->nb_frames > 0 ? (unsigned)vs->nb_frames : (unsigned)(vs->duration > 0 ? vs->duration : 1);
    b->pub.FrameNum = 1;

    b->vframe = av_frame_alloc();
    b->aframe = av_frame_alloc();
    b->pkt = av_packet_alloc();

    if (b->adec != NULL && ff_snd_open != NULL) {
        int ch = b->adec->ch_layout.nb_channels;
        if (ff_snd_open(&b->snd, (u32)b->adec->sample_rate, 16, ch, 0, (HBINK)b)) {
            b->snd_ok = true;
            if (b->snd.SetOnOff != NULL) {
                b->snd.SetOnOff(&b->snd, 1);
            }
        }
    }

    SDL_Log("Bink (FFmpeg): %s %ux%u, %u frames @ %.2f fps, audio %s",
        name, b->pub.Width, b->pub.Height, b->pub.Frames, b->fps, b->snd_ok ? "on" : "off");
    return (HBINK)b;
}

static int ff_wait(HBINK bnk)
{
    BinkFF* b = (BinkFF*)bnk;
    Uint64 now = SDL_GetTicksNS();
    if (!b->started) {
        b->started = true;
        b->start_ns = now;
        return 0;
    }
    Uint64 due = b->start_ns + (Uint64)(((double)(b->pub.FrameNum - 1) / b->fps) * 1e9);
    if (now < due) {
        SDL_DelayNS(SDL_min(due - now, (Uint64)2000000));
        return 1;
    }
    return 0;
}

static int ff_do_frame(HBINK bnk)
{
    BinkFF* b = (BinkFF*)bnk;

    while (!b->eof) {
        int rc = avcodec_receive_frame(b->vdec, b->vframe);
        if (rc == 0) {
            return 0;
        }

        if (av_read_frame(b->fmt, b->pkt) < 0) {
            b->eof = true;
            avcodec_send_packet(b->vdec, NULL);
            if (avcodec_receive_frame(b->vdec, b->vframe) == 0) {
                return 0;
            }
            break;
        }

        if (b->pkt->stream_index == b->vidx) {
            avcodec_send_packet(b->vdec, b->pkt);
        } else if (b->pkt->stream_index == b->aidx && b->adec != NULL) {
            if (avcodec_send_packet(b->adec, b->pkt) == 0) {
                drain_audio(b);
            }
        }
        av_packet_unref(b->pkt);
    }

    return 0;
}

static inline uint8_t clamp8(int v)
{
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static int ff_copy_to_buffer(HBINK bnk, void* dest, int destpitch, unsigned destheight, unsigned destx, unsigned desty, unsigned flags)
{
    BinkFF* b = (BinkFF*)bnk;
    const AVFrame* f = b->vframe;
    (void)flags;

    if (f->data[0] == NULL) {
        return 0;
    }

    int w = f->width;
    int h = f->height;
    if (h > (int)destheight - (int)desty) {
        h = (int)destheight - (int)desty;
    }

    // BT.601 limited-range YUV 4:2:0 -> XRGB8888 (bytes B, G, R, X).
    for (int y = 0; y < h; y++) {
        const uint8_t* yr = f->data[0] + (size_t)y * f->linesize[0];
        const uint8_t* ur = f->data[1] + (size_t)(y / 2) * f->linesize[1];
        const uint8_t* vr = f->data[2] + (size_t)(y / 2) * f->linesize[2];
        uint8_t* d = (uint8_t*)dest + (size_t)(desty + y) * destpitch + (size_t)destx * 4;
        for (int x = 0; x < w; x++) {
            int c = 298 * (yr[x] - 16);
            int u = ur[x / 2] - 128;
            int v = vr[x / 2] - 128;
            d[0] = clamp8((c + 516 * u + 128) >> 8);
            d[1] = clamp8((c - 100 * u - 208 * v + 128) >> 8);
            d[2] = clamp8((c + 409 * v + 128) >> 8);
            d[3] = 0xFF;
            d += 4;
        }
    }

    return 0;
}

static void ff_next_frame(HBINK bnk)
{
    BinkFF* b = (BinkFF*)bnk;
    av_frame_unref(b->vframe);
    b->pub.FrameNum++;
}

static int ff_dd_surface_type(void* lpDDS)
{
    (void)lpDDS;
    return 3;
}

static BINKSNDOPEN ff_open_miles(void* param)
{
    (void)param;
    return NULL;
}

static int ff_set_sound_system(BINKSNDSYSOPEN open, void* param)
{
    ff_snd_open = open != NULL ? open(param) : NULL;
    return 1;
}

static void ff_set_sound_track(unsigned track)
{
    ff_sound_track = track;
}

void bink_ffmpeg_bind(BinkFfmpegApi* api)
{
    av_log_set_level(AV_LOG_ERROR);
    api->close = ff_close;
    api->copy_to_buffer = ff_copy_to_buffer;
    api->dd_surface_type = ff_dd_surface_type;
    api->do_frame = ff_do_frame;
    api->next_frame = ff_next_frame;
    api->open = ff_open;
    api->open_miles = ff_open_miles;
    api->set_sound_system = ff_set_sound_system;
    api->set_sound_track = ff_set_sound_track;
    api->wait = ff_wait;
}
