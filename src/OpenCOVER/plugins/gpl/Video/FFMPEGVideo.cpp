#define __STDC_CONSTANT_MACROS
#include "FFMPEGVideo.h"

#include <config/CoviseConfig.h>

#include <cover/coVRTui.h>
#include <cover/VRSceneGraph.h>
#include <cover/coVRMSController.h>
#include <cover/coVRConfig.h>
#include <config/coConfigConstants.h>

#ifdef WIN32
#include <sys/timeb.h>
#else
#include <sys/time.h>
#endif

#if XERCES_VERSION_MAJOR < 3
#include <xercesc/dom/DOMWriter.hpp>
#else
#include <xercesc/dom/DOMLSSerializer.hpp>
#endif
#include <xercesc/framework/LocalFileFormatTarget.hpp>
#include <xercesc/parsers/XercesDOMParser.hpp>
#include <xercesc/util/XMLUni.hpp>

#include <stdio.h>

using namespace covise;

void FFMPEGPlugin::video_tag(const char *cname)
{
    char *tail;
#ifdef HAVE_CODEC_CTX_PTR
    video_st->codec->codec_tag = strtol(cname, &tail, 0);
#else
    video_st->codec.codec_tag = strtol(cname, &tail, 0);
#endif

    if (!tail || *tail)
    {
#ifdef HAVE_CODEC_CTX_PTR
        video_st->codec->codec_tag = cname[0] + (cname[1] << 8) + (cname[2] << 16) + (cname[3] << 24);
#else
        video_st->codec.codec_tag = cname[0] + (cname[1] << 8) + (cname[2] << 16) + (cname[3] << 24);
#endif
    }
}

/* add a video output stream */
bool FFMPEGPlugin::add_video_stream(AVCodec *codec, int w, int h, int frame_rate, int bitrate, int maxBitrate)
{
#if FF_API_NEW_STREAM
    video_st = av_new_stream(oc, 0);
#else
    video_st = avformat_new_stream(oc, NULL);
    if (video_st)
    {
        video_st->id = 0;
    }
#endif

    if (!video_st)
    {
        fprintf(stderr, "Could not alloc stream\n");
        return false;
    }

#ifdef HAVE_CODEC_CTX_PTR
    codecCtx = video_st->codec;
#else
    codecCtx = &video_st->codec;
#endif

    codecCtx->codec_id = codec->id;
    codecCtx->codec_type = codec->type;

    /* resolution must be a multiple of two */
    codecCtx->width = w;
    codecCtx->height = h;
    /* time base: this is the fundamental unit of time (in seconds) in terms
      of which frame timestamps are represented. for fixed-fps content,
      timebase should be 1/framerate and timestamp increments should be
      identically 1. */

    AVRational fps;
    fps.num = frame_rate;
    fps.den = 1;

    if (codec->supported_framerates)
    {
        int i = 0;
        while (!((codec->supported_framerates[i].num == 0) && (codec->supported_framerates[i].den == 0)) && (frame_rate != codec->supported_framerates[i].num))
            i++;

        if (frame_rate != codec->supported_framerates[i].num)
        {
            fprintf(stderr, "Framerate not supported\n");
            return false;
        }
    }

    codecCtx->time_base.den = fps.num;
    codecCtx->time_base.num = fps.den;
    //   myPlugin->selectFrameRate->setSelectedText(fps.num);
    codecCtx->bit_rate = bitrate;
    codecCtx->rc_min_rate = 0;
    codecCtx->rc_max_rate = maxBitrate;

    /*  sample parameters */
    if (codecCtx->width * codecCtx->height <= 352 * 288) /*352*288,320x240*/
    {
        codecCtx->rc_min_rate = 1150000;
        codecCtx->rc_buffer_size = 224 * 1024 * 8;
        oc->packet_size = 6144;
    }
    else if (codecCtx->width * codecCtx->height <= 640 * 480)
    {
        codecCtx->bit_rate = 2040000;
        codecCtx->rc_min_rate = 0;
        codecCtx->rc_max_rate = 2516000;
        codecCtx->rc_buffer_size = 352 * 1024 * 8;
        oc->packet_size = 10240;
    }
    else if (codecCtx->width * codecCtx->height <= 768 * 576) /* 720x576, 768x576 */
    {
        codecCtx->bit_rate = 6000000; /*720x576,768x576*/
        codecCtx->rc_min_rate = 0;
        codecCtx->rc_max_rate = 9000000;
        codecCtx->rc_buffer_size = 512 * 1024 * 8;
        oc->packet_size = 12288;
    }
    else
    {

        codecCtx->rc_buffer_size = 1024 * 1024 * 8; /*buffer_size, packet_size noch anpassen */
        oc->packet_size = 24000;
    }
    /* To avoid underflow problems change buffer and packet size */
    codecCtx->mb_decision = FF_MB_DECISION_RD;
    codecCtx->bit_rate_tolerance = 4000 * 1000;
    codecCtx->rc_buffer_aggressivity = 1.0;

#if !defined(HAVE_LIBAV)
# if LIBAVFORMAT_VERSION_MAJOR < 54
    oc->mux_rate = 2352 * 75 * 8;
    oc->preload = (int)(100 * AV_TIME_BASE);
# else
    oc->audio_preload = (int)(100 * AV_TIME_BASE);
# endif
#endif

    oc->max_delay = (int)(0.7 * AV_TIME_BASE);

    codecCtx->gop_size = 12; /* emit one intra frame every twelve frames at most */

    if (codec->id == AV_CODEC_ID_RAWVIDEO)
    {
        codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    }
    else if (!codec || !codec->pix_fmts || (codec->pix_fmts[0] == -1)) // pix_fmts is terminated by -1
    {
        fprintf(stderr, "automatic video pixel format determination failed\n");
        return false;
    }
    else
    {
        codecCtx->pix_fmt = codec->pix_fmts[0]; // pix_fmts[0] should be the the best suited pix_fmt let's just
    }
    // use that one. BTW: here we already know it's defined

    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
    {
        codecCtx->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }

    return true;
}

AVFrame *FFMPEGPlugin::alloc_picture(AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame *picture;
    uint8_t *picture_buf;
    int size;

    picture = av_frame_alloc();
    if (!picture)
        return NULL;
    size = avpicture_get_size(pix_fmt, width, height);
    picture_buf = (uint8_t *)av_malloc(size);
    if (!picture_buf)
    {
        av_free(picture);
        return NULL;
    }
    avpicture_fill((AVPicture *)picture, picture_buf, pix_fmt, width, height);
    return picture;
}

bool FFMPEGPlugin::open_codec(AVCodec *codec)
{

    if (!codec)
    {
        fprintf(stderr, "codec with id %d  not found\n", codecCtx->codec_id);
        return false;
    }

/* open the codec */
#if LIBAVCODEC_VERSION_MAJOR < 54
    if (avcodec_open(codecCtx, codec) < 0)
#else
    if (avcodec_open2(codecCtx, codec, NULL) < 0)
#endif
    {
        fprintf(stderr, "could not open codec\n");
        return false;
    }

    return true;
}

bool FFMPEGPlugin::open_video(AVCodec *codec)
{
    if (!open_codec(codec))
        return false;
    video_outbuf = NULL;

    video_outbuf_size = avpicture_get_size(codecCtx->pix_fmt, codecCtx->width, codecCtx->height);
    video_outbuf = (uint8_t *)av_malloc(video_outbuf_size);

    /* allocate the encoded raw picture */
    picture = alloc_picture(codecCtx->pix_fmt, myPlugin->inWidth, myPlugin->inHeight);
    if (!picture)
    {
        fprintf(stderr, "Could not allocate picture\n");
        return false;
    }

    /* if the output format is not RGBA32, then a temporary YUV420P
      picture is needed too. It is then converted to the required
      output format */
    inPicture = av_frame_alloc();
    if (!inPicture)
    {
        fprintf(stderr, "Could not allocate temporary picture\n");
        return false;
    }

    outPicture = NULL;
#ifdef HAVE_SWSCALE_H
    if (myPlugin->resize || (codecCtx->pix_fmt != myPlugin->capture_fmt))
    {
        if (myPlugin->inWidth * myPlugin->inHeight < codecCtx->width * codecCtx->height)
        {
            swsconvertctx = sws_getContext(myPlugin->inWidth, myPlugin->inHeight, myPlugin->capture_fmt,
                                           codecCtx->width, codecCtx->height, codecCtx->pix_fmt,
                                           myPlugin->resize ? SWS_BICUBIC : SWS_POINT, NULL, NULL, NULL);
        }
        else
        {
            swsconvertctx = sws_getContext(myPlugin->inWidth, myPlugin->inHeight, myPlugin->capture_fmt,
                                           codecCtx->width, codecCtx->height, codecCtx->pix_fmt,
                                           myPlugin->resize ? SWS_BILINEAR : SWS_POINT, NULL, NULL, NULL);
        }
        if (swsconvertctx == NULL)
        {
            fprintf(stderr, "Cannot initialize the conversion context!\n");
            return false;
        }
    }
    outPicture = alloc_picture(codecCtx->pix_fmt, codecCtx->width, codecCtx->height);
#else
    if (myPlugin->resize)
    {
        imgresamplectx = img_resample_init(codecCtx->width, codecCtx->height, myPlugin->inWidth, myPlugin->inHeight);
        outPicture = alloc_picture(codecCtx->pix_fmt, codecCtx->width, codecCtx->height);
    }
#endif

    return true;
}

FFMPEGPlugin::FFMPEGPlugin()
{
    fmt = NULL;
    oc = NULL;
    video_st = NULL;
    codecCtx = NULL;
    picture = inPicture = outPicture = NULL;
#ifdef HAVE_SWSCALE_H
    swsconvertctx = NULL;
#else
    imgresamplectx = NULL;
#endif
    video_outbuf = NULL;
    mirroredpixels = NULL;
}

FFMPEGPlugin::~FFMPEGPlugin()
{
    delete paramNameField;
    delete paramErrorLabel;
    delete paramLabel;
    delete bitrateField;
    delete maxBitrateField;
    delete saveButton;
    delete bitrateLabel;
    delete maxBitrateLabel;

    delete selectCodec;
    delete selectParams;
    close_video();

    if (video_st)
        close_video();
    if (oc)
        av_free(oc);
    if (myPlugin->pixels)
        av_free(myPlugin->pixels);
    if (mirroredpixels)
        av_free(mirroredpixels);
}

void FFMPEGPlugin::unInitialize()
{
    if (video_st)
        close_video();

    if (oc)
    {
        if (oc->pb) /* close the output file */
#if LIBAVFORMAT_VERSION_INT <= AV_VERSION_INT(51, 19, 0)
            url_fclose(&oc->pb);
#elif LIBAVFORMAT_VERSION_INT <= AV_VERSION_INT(52, 64, 2)
            url_fclose(oc->pb);
#else
            avio_close(oc->pb);
#endif

        for (unsigned int i = 0; i < oc->nb_streams; i++)
        {
            av_freep(&oc->streams[i]);
        }

        av_free(oc);
        oc = NULL;
    }

    ifstream ifs(myPlugin->fileNameField->getText().c_str(), ifstream::in);
    if (!ifs.fail())
    {
        ifs.close();
        unlink(myPlugin->fileNameField->getText().c_str());
    }
}

void FFMPEGPlugin::close_video()
{
    if (picture)
    {
        av_free(picture->data[0]);
        av_free(picture);
        picture = NULL;
    }
    if (inPicture)
    {
        av_free(inPicture);
        inPicture = NULL;
    }

#ifdef HAVE_SWSCALE_H
    if ((codecCtx) && (myPlugin->resize || (codecCtx->pix_fmt != myPlugin->capture_fmt)))
    {
        if (swsconvertctx)
            sws_freeContext(swsconvertctx);
        if (outPicture)
        {
            av_free(outPicture->data[0]);
            av_free(outPicture);
            outPicture = NULL;
        }
    }
#else
    if (myPlugin->resize)
    {
        if (imgresamplectx)
            img_resample_close(imgresamplectx);
        if (outPicture)
        {
            av_free(outPicture->data[0]);
            av_free(outPicture);
            outPicture = NULL;
        }
    }
#endif
    if (codecCtx)
    {
        avcodec_close(codecCtx);
        codecCtx = NULL;
    }
    if (video_outbuf)
    {
        av_free(video_outbuf);
        video_outbuf = NULL;
    }
}

void FFMPEGPlugin::close_all(bool stream, int format)
{
    int i;

    /* write the trailer, if any */
    av_write_trailer(oc);

    /* close each codec */
    if (video_st)
        close_video();

    if (oc)
    {
        /* free the streams */
        for (i = 0; i < oc->nb_streams; i++)
        {
            av_freep(&oc->streams[i]);
        }

        if (oc->pb) /* close the output file */
#if LIBAVFORMAT_VERSION_INT <= AV_VERSION_INT(51, 19, 0)
            url_fclose(&oc->pb);
#elif LIBAVFORMAT_VERSION_INT <= AV_VERSION_INT(52, 64, 2)
            url_fclose(oc->pb);
#else
            avio_close(oc->pb);
#endif

        /* free the stream */
        av_free(oc);
        oc = NULL;
    }

    av_free(myPlugin->pixels);
    myPlugin->pixels = NULL;
    av_free(mirroredpixels);
    mirroredpixels = NULL;
}

void FFMPEGPlugin::init_GLbuffers()
{
    int size;
    size = avpicture_get_size(myPlugin->capture_fmt, myPlugin->inWidth, myPlugin->inHeight);
    myPlugin->pixels = (uint8_t *)av_malloc(size);
    mirroredpixels = (uint8_t *)av_malloc(size);
}

bool FFMPEGPlugin::videoCaptureInit(const string &filename, int format, int RGBFormat)
{

    if (RGBFormat == 1)
        myPlugin->GL_fmt = GL_BGRA;
    linesize = myPlugin->inWidth * 4;

    if (!FFMPEGInit(NULL, NULL, filename, false))
    {
        myPlugin->errorLabel->setLabel("Memory error, codec not installed or invalid output parameters");
        myPlugin->captureButton->setState(false);
        if (video_st)
            close_video();
        return false;
    }
    return true;
}

bool FFMPEGPlugin::FFMPEGInit(AVOutputFormat *outfmt, AVCodec *codec, const string &filename,
                              bool test_codecs)
{
    impl = xercesc::DOMImplementationRegistry::getDOMImplementation(xercesc::XMLString::transcode("Core"));

/* allocate the output media context */
// need this for newer version todo: #ifdef version oc = avformat_alloc_context();
#if LIBAVFORMAT_VERSION_INT < (53 << 16)
    oc = av_alloc_format_context();
#else
    oc = avformat_alloc_context();
#endif
    if (!oc)
    {
        fprintf(stderr, "Memory error\n");
        return (false);
    }

    if (!test_codecs) /* user selected format and codec */
    {
        /* Chosen Format */
        std::map<AVOutputFormat *, AVCodecList>::iterator it = formatList.begin();

        if (myPlugin->selectFormat->getSelectedEntry() < 0)
            myPlugin->selectFormat->setSelectedEntry(0);
        for (; it != formatList.end(); it++)
        {
            if (strcmp((*it).first->long_name, myPlugin->selectFormat->getSelectedText().c_str()) == 0)
            {
                fmt = (*it).first;
                break;
            }
        }
        if (it == formatList.end())
        {
            fprintf(stderr, "Format not available on %d\n", coVRMSController::instance()->getID());
            return (false);
        }

        oc->oformat = fmt;
        snprintf(oc->filename, sizeof(oc->filename), "%s", filename.c_str());

        /* Chosen codec */
        video_st = NULL;

        if (selectCodec->getSelectedEntry() < 0)
            selectCodec->setSelectedEntry(0);
        std::list<AVCodec *>::iterator itlist;

        for (itlist = (*it).second.begin(); itlist != (*it).second.end(); itlist++)
        {
            if (strcmp((*itlist)->long_name, selectCodec->getSelectedText().c_str()) == 0)
            {
                fmt->video_codec = (*itlist)->id;
                codec = *itlist;
                break;
            }
        }
        if (itlist == (*it).second.end())
            fmt->video_codec = AV_CODEC_ID_NONE;

        /* Configure video stream */
        if (fmt->video_codec != AV_CODEC_ID_NONE)
        {
            if (!add_video_stream(codec, myPlugin->outWidth, myPlugin->outHeight, myPlugin->time_base,
                                  bitrateField->getValue() * 1000, maxBitrateField->getValue() * 1000))
                return false;
        }
        else
        {
            fprintf(stderr, "Codec not installed on %d\n", coVRMSController::instance()->getID());
            av_free(oc);
            video_st = NULL;
            return (false);
        }
    }
    else /* test installed codec */
    {
        fmt = outfmt;
        oc->oformat = fmt;
#ifdef AVFMT_NODIMENSIONS
        if (fmt->flags & AVFMT_NODIMENSIONS)
        {
            fprintf(stderr, "image format\n");
            return false;
        }
#endif

        video_st = NULL;
        fmt->video_codec = codec->id;

        if (fmt->video_codec != AV_CODEC_ID_NONE)
        {
            if (fmt->video_codec == AV_CODEC_ID_DVVIDEO)
            {
                if (!add_video_stream(codec, 720, 576, 25, 6000000, 9000000))
                    return false;
            }
            else if (strcmp(fmt->name, "gxf") == 0)
            {
                if (!add_video_stream(codec, 768, 576, 25, 6000000, 9000000))
                    return false;
            }
            else if (!add_video_stream(codec, 352, 288, 25, 1150000, 1150000))
                return false;
        }
        else
        {
            fprintf(stderr, "Codec not installed\n");
            av_free(oc);
            return (false);
        }
    }

#if LIBAVFORMAT_VERSION_INT <= AV_VERSION_INT(52, 64, 2)

    /* set the output parameters (must be done even if no parameters). */
    if (av_set_parameters(oc, NULL) < 0)
    {
        fprintf(stderr, "Invalid output format parameters\n");
        if (video_st)
            close_video();
        av_free(oc);
        return (false);
    }
    dump_format(oc, 0, filename.c_str(), 1);
#else
    av_dump_format(oc, 0, filename.c_str(), 1);
#endif

    /* now that all the parameters are set, we can open the audio and
      video codecs and allocate the necessary encode buffers */
    if (video_st)
    {

        if ((test_codecs && !open_codec(codec)) || (!test_codecs && !open_video(codec)))
        {
            // FIXME this leaks memory
            video_st = NULL;
            return false;
        }
    }

/* open the output file, if needed */
#if LIBAVFORMAT_VERSION_INT <= AV_VERSION_INT(52, 111, 0)
    if (!(fmt->flags & AVFMT_NOFILE))
        url_fopen(&oc->pb, filename.c_str(), URL_WRONLY);
#else
    if (!(fmt->flags & AVFMT_NOFILE))
        avio_open(&oc->pb, filename.c_str(), AVIO_FLAG_WRITE);
#endif

/* write the stream s, if any */
#if LIBAVFORMAT_VERSION_INT <= AV_VERSION_INT(52, 108, 0)
    if (av_write_header(oc) < 0)
#else
    if (avformat_write_header(oc, NULL))
#endif
    {
        fprintf(stderr, "Could not write header  (incorrect codec parameters ?)");
        return false;
    }

    return (true);
}

void FFMPEGPlugin::checkFileFormat(const string &filename)
{
    char buf[1000];

    ofstream dataFile(filename.c_str(), ios::ate);
    if (!dataFile)
    {
        sprintf(buf, "Could not open file. Please check file name.");
        myPlugin->fileErrorLabel->setLabel(buf);
        myPlugin->fileError = true;
        return;
    }
    else
    {
        dataFile.close();
        myPlugin->fileError = false;
    }

    myPlugin->sizeError = myPlugin->opt_frame_size(myPlugin->outWidth, myPlugin->outHeight);
    if (!myPlugin->sizeError)
    {

        const char *codecName, *formatName;
        std::map<AVOutputFormat *, AVCodecList>::iterator it = formatList.begin();
        int formatEntry = myPlugin->selectFormat->getSelectedEntry();
        if (formatEntry < 0)
            formatName = (*formatList.begin()).first->long_name;
        else
        {
            formatName = myPlugin->selectFormat->getSelectedText().c_str();
            for (int i = 0; i < formatEntry; i++)
                it++;
        }

        if (selectCodec->getSelectedEntry() < 0)
            codecName = (*(*it).second.begin())->long_name;
        else
            codecName = selectCodec->getSelectedText().c_str();

        if (strcmp(codecName, "DV (Digital Video)") == 0)
        {
            if (!((myPlugin->outWidth == 720) && (myPlugin->outHeight == 576)) && !((myPlugin->outWidth == 720) && (myPlugin->outHeight == 480)))
            {
                sprintf(buf, "DV only supports 720x576 or 720x480");
                myPlugin->errorLabel->setLabel(buf);
                myPlugin->sizeError = true;
                return;
            }
        }
        else if (strcmp(formatName, "GXF format") == 0)
        {
            if ((myPlugin->outHeight != 480) && (myPlugin->outHeight != 512) && (myPlugin->outHeight != 576) && (myPlugin->outHeight != 608))
            {
                sprintf(buf, "gxf muxer only accepts PAL or NTSC resolutions");
                myPlugin->errorLabel->setLabel(buf);
                myPlugin->sizeError = true;
                return;
            }
        }
    }
}

#ifndef HAVE_SWSCALE_H
int VideoPlugin::ImgConvertScale(int width, int height)
{
    int y, out_size;

    // 	OpenGL reads bottom-to-top, encoder expects top-to-bottom
    for (y = height; y > 0; y--)
        memcpy(&mirroredpixels[(height - y) * linesize], &pixels[(y - 1) * linesize], linesize);

    if (codecCtx->pix_fmt == AV_PIX_FMT_YUV420P)
    {
        avpicture_fill((AVPicture *)inPicture, mirroredpixels, myPlugin->capture_fmt, width, height);

        if (img_convert((AVPicture *)picture, codecCtx->pix_fmt, (AVPicture *)inPicture, myPlugin->capture_fmt,
                        width, height) < 0)
        {
            fprintf(stderr, "Could not convert picture\n");
            exit(1);
        }
    }
    else if (codecCtx->pix_fmt != myPlugin->capture_fmt)
    {
        avpicture_fill((AVPicture *)inPicture, mirroredpixels, myPlugin->capture_fmt, width, height);
        img_convert((AVPicture *)picture, codecCtx->pix_fmt, (AVPicture *)inPicture, myPlugin->capture_fmt,
                    width, height);
    }
    else
    {
        avpicture_fill((AVPicture *)picture, mirroredpixels, myPlugin->capture_fmt, width, height);
    }

    // 	scale the image
    if (resize)
    {
        img_resample(imgresamplectx, (AVPicture *)outPicture, (AVPicture *)picture);
        out_size = avcodec_encode_video(codecCtx, video_outbuf, video_outbuf_size, outPicture);
    }
    else
        /* encode the image */
        out_size = avcodec_encode_video(codecCtx, video_outbuf, video_outbuf_size, picture);

    return (out_size);
}

#else
int FFMPEGPlugin::SwConvertScale(int width, int height)
{
    //  	OpenGL reads bottom-to-top, encoder expects top-to-bottom
    for (int y = height; y > 0; y--)
        memcpy(&mirroredpixels[(height - y) * linesize], &myPlugin->pixels[(y - 1) * linesize], linesize);

    AVFrame *result = NULL;

    if (myPlugin->resize || (codecCtx->pix_fmt != myPlugin->capture_fmt))
    {
        avpicture_fill((AVPicture *)inPicture, mirroredpixels, myPlugin->capture_fmt, width, height);

        sws_scale(swsconvertctx, inPicture->data, inPicture->linesize, 0, height, outPicture->data,
                  outPicture->linesize);
        result = outPicture;
    }
    else
    {
        avpicture_fill((AVPicture *)inPicture, mirroredpixels, myPlugin->capture_fmt, width, height);
        result = inPicture;
    }

    result->pts = myPlugin->frameCount;

    /* encode the image */
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(57, 24, 102)
    return avcodec_encode_video(codecCtx, video_outbuf, video_outbuf_size, result);
#else
  AVPacket pkt;
  av_init_packet(&pkt);
  pkt.data = (uint8_t*)video_outbuf;
  pkt.size = video_outbuf_size;
  int got_output=0;
  int ret = avcodec_encode_video2(codecCtx, &pkt, inPicture, &got_output);
  if (ret < 0)
  {
    return -1;
  }
  return got_output ? pkt.size : 0;
#endif
}
#endif

void FFMPEGPlugin::videoWrite(int format)
{
    int ret, out_size;

//        #ifdef SWSCALE
#ifdef HAVE_SWSCALE_H
    out_size = SwConvertScale(myPlugin->inWidth, myPlugin->inHeight);
#else
    out_size = ImgConvertScale(myPlugin->inWidth, myPlugin->inHeight);
#endif

    /* if zero size, it means the image was buffered */
    if (out_size > 0)
    {
        av_init_packet(&pkt);

        pkt.stream_index = video_st->index;
        pkt.data = video_outbuf;
        pkt.size = out_size;
        pkt.pts = av_rescale_q(codecCtx->coded_frame->pts, codecCtx->time_base, video_st->time_base);
#ifdef AV_PKT_FLAG_KEY
        if (codecCtx->coded_frame->key_frame)
            pkt.flags |= AV_PKT_FLAG_KEY;
#else
        if (codecCtx->coded_frame->key_frame)
            pkt.flags |= PKT_FLAG_KEY;
#endif

        /* write the compressed frame in the media file */
        ret = av_write_frame(oc, &pkt);
        myPlugin->frameCount++;
        if (cover->frameTime() - myPlugin->starttime >= 1)
        {
            myPlugin->showFrameCountField->setValue(myPlugin->frameCount);
            myPlugin->starttime = cover->frameTime();
        }
    }
    else
        ret = 0;
}

void FFMPEGPlugin::ListFormatsAndCodecs(const string &filename)
{

    av_register_all();
#ifdef AVCODEC_DSPUTIL_H
    ff_dsputil_static_init();
#else
#if LIBAVFORMAT_VERSION_MAJOR < 54
    avcodec_init();
#else
// avcodec_init() has been removed
#endif
#endif

    AVOutputFormat *format = av_oformat_next(NULL);
    AVCodec *codec;
    std::list<AVCodec *> codecList;

    codec = av_codec_next(NULL);
    do
    {
#if LIBAVCODEC_VERSION_MAJOR >= 53
        if (codec->type == AVMEDIA_TYPE_VIDEO)
#else
        if (codec->type == CODEC_TYPE_VIDEO)
#endif
        {
            if (avcodec_find_encoder(codec->id) && avcodec_find_decoder(codec->id))
            {
                std::list<AVCodec *>::iterator it = codecList.begin();
                for (; it != codecList.end(); it++)
                    if ((*it)->id > codec->id)
                    {
                        codecList.insert(it, codec);
                        break;
                    }
                    else if ((*it)->id == codec->id)
                        break;

                if (it == codecList.end())
                    codecList.push_back(codec);
            }
        }

        codec = av_codec_next(codec);
    } while (codec != NULL);

    std::list<AVCodec *>::iterator it = codecList.begin();

    AVOutputFormat *lastFormat = NULL;
    while (format != NULL)
    {

        if (((!lastFormat) || (strcmp(format->long_name, lastFormat->long_name) != 0)) && (format->video_codec != 0))
        {

            list<AVCodec *>::iterator it;
            AVCodecList sublist;
            AVCodec *last = NULL;

            if (format->codec_tag != 0)
            {
#if LIBAVCODEC_VERSION_MAJOR < 54
                CodecID cID = format->video_codec;
#else
                AVCodecID cID = format->video_codec;
#endif
                for (it = codecList.begin(); it != codecList.end(); it++)
                {
#if LIBAVFORMAT_VERSION_INT < (53 << 16)
                    if (((!last) || (strcmp((*it)->long_name, last->long_name) != 0)) && (((*it)->id == AV_CODEC_ID_RAWVIDEO) || (av_codec_get_tag((const struct AVCodecTag **)format->codec_tag, (*it)->id) > 0)) && (coVRMSController::instance()->isSlave() || (coVRMSController::instance()->isMaster() && FFMPEGInit(format, *it, filename, true))))
#else
                    if (((!last) || (strcmp((*it)->long_name, last->long_name) != 0)) && (((*it)->id == AV_CODEC_ID_RAWVIDEO) || (av_codec_get_tag(format->codec_tag, (*it)->id) > 0)) && (coVRMSController::instance()->isSlave() || (coVRMSController::instance()->isMaster() && FFMPEGInit(format, *it, filename, true))))
#endif
                    {

                        if ((*it)->id == cID)
                            sublist.push_front((*it));
                        else
                            sublist.push_back((*it));
                        last = (*it);
                        unInitialize();
                    }
                }
            }
            else
            {
                for (it = codecList.begin(); it != codecList.end(); it++)
                {
                    if (((*it)->id == format->video_codec) && (coVRMSController::instance()->isSlave() || (coVRMSController::instance()->isMaster() && FFMPEGInit(format, *it, filename, true))))
                    {
                        sublist.push_back((*it));
                        unInitialize();
                        break;
                    }
                }
            }

            if (!sublist.empty())
                formatList.insert(pair<AVOutputFormat *, AVCodecList>(format, sublist));
            lastFormat = format;
        }

        do
        {
            format = av_oformat_next(format);
        } while (formatList.find(format) != formatList.end());
    };
}

void FFMPEGPlugin::FillComboBoxSetExtension(int selection, int row)
{
    int count;

    selectCodec = new coTUIComboBox("Codec:", myPlugin->VideoTab->getID());
    selectCodec->setPos(0, row);
    selectCodec->setEventListener(this);

    if (coVRMSController::instance()->isMaster())
    {
        std::map<AVOutputFormat *, AVCodecList>::iterator it = formatList.begin();
        for (int i = 0; i < selection; i++)
            it++;

        if ((*it).first->extensions)
        {
            filterList = "";
            char *ext = new char[strlen((*it).first->extensions) + 1];
            strcpy(ext, (*it).first->extensions);
            char *pchar = strtok(ext, ",");
            while (pchar != NULL)
            {
                std::string p = pchar;
                if (filterList != "")
                    filterList = filterList + "; *." + p;
                else
                    filterList = "*." + p;
                pchar = strtok(NULL, ",");
            }
            myPlugin->fileNameBrowser->setFilterList(filterList);
        }

        int length = filterList.length();
        coVRMSController::instance()->sendSlaves((char *)&length, sizeof(int));
        coVRMSController::instance()->sendSlaves(filterList.c_str(), length + 1);
        count = (*it).second.size();
        coVRMSController::instance()->sendSlaves((char *)&count, sizeof(int));
        std::list<AVCodec *>::iterator itlist;
        for (itlist = (*it).second.begin(); itlist != (*it).second.end(); itlist++)
        {
            selectCodec->addEntry((*itlist)->long_name);
            length = strlen((*itlist)->long_name);
            coVRMSController::instance()->sendSlaves((char *)&length, sizeof(length));
            coVRMSController::instance()->sendSlaves((*itlist)->long_name, length + 1);
        }
    }
    else
    {

        int length;
        coVRMSController::instance()->readMaster((char *)&length, sizeof(int));
        char *charString = new char[length + 1];
        coVRMSController::instance()->readMaster(charString, length + 1);
        filterList = charString;
        delete[] charString;
        coVRMSController::instance()->readMaster((char *)&count, sizeof(int));
        for (int i = 0; i < count; i++)
        {
            coVRMSController::instance()->readMaster((char *)&length, sizeof(length));
            char *charString = new char[length + 1];
            coVRMSController::instance()->readMaster(charString, length + 1);
            selectCodec->addEntry(string(charString));
            delete[] charString;
        }
    }
}

void FFMPEGPlugin::ParamMenu(int row)
{

    bitrateLabel = new coTUILabel("Average Bitrate kbit/s:", myPlugin->VideoTab->getID());
    bitrateLabel->setPos(0, row);
    bitrateLabel->setColor(Qt::black);
    bitrateField = new coTUIEditIntField("average bitrate kbit/s", myPlugin->VideoTab->getID());
    bitrateField->setValue(4000);
    bitrateField->setEventListener(this);
    bitrateField->setPos(1, row);

    maxBitrateLabel = new coTUILabel("Maximum Bitrate kbit/s:", myPlugin->VideoTab->getID());
    maxBitrateLabel->setPos(2, row);
    maxBitrateLabel->setColor(Qt::black);
    maxBitrateField = new coTUIEditIntField("maximum bitrate kbit/s", myPlugin->VideoTab->getID());
    maxBitrateField->setValue(6000);
    maxBitrateField->setEventListener(this);
    maxBitrateField->setPos(3, row);

    saveButton = new coTUIToggleButton("Enter Description and Save Parameter", myPlugin->VideoTab->getID());
    saveButton->setEventListener(this);
    saveButton->setState(false);
    saveButton->setPos(1, row + 2);

    paramNameField = new coTUIEditField("paramsDescription", myPlugin->VideoTab->getID());
    paramNameField->setText("");
    paramNameField->setEventListener(this);
    paramNameField->setPos(0, row + 2);

    paramErrorLabel = new coTUILabel("", myPlugin->VideoTab->getID());
    paramErrorLabel->setPos(0, row + 3);
    paramErrorLabel->setColor(Qt::black);
}

void FFMPEGPlugin::hideParamMenu(bool hide)
{
    bitrateLabel->setHidden(hide);
    bitrateField->setHidden(hide);
    maxBitrateLabel->setHidden(hide);
    maxBitrateField->setHidden(hide);
    saveButton->setHidden(hide);
    paramNameField->setHidden(hide);
    paramLabel->setHidden(hide);
    paramErrorLabel->setHidden(hide);
}

void FFMPEGPlugin::fillParamComboBox(int row)
{
    paramLabel = new coTUILabel("Parameter Dataset", myPlugin->VideoTab->getID());
    paramLabel->setPos(0, row);
    paramLabel->setColor(Qt::black);

    selectParams = new coTUIComboBox("Params:", myPlugin->VideoTab->getID());
    selectParams->setPos(1, row);
    selectParams->setEventListener(this);

    std::list<VideoParameter>::iterator it;
    for (it = VPList.begin(); it != VPList.end(); it++)
    {
        selectParams->addEntry((*it).name);
    }
}

void FFMPEGPlugin::Menu(int row)
{
    int count;

    ParamMenu(row + 2);
    ListFormatsAndCodecs(myPlugin->fileNameField->getText());
    if (coVRMSController::instance()->isMaster())
    {
        count = formatList.size();
        coVRMSController::instance()->sendSlaves((char *)&count, sizeof(int));
    }
    else
        coVRMSController::instance()->readMaster((char *)&count, sizeof(int));

    if (coVRMSController::instance()->isMaster())
    {
        std::map<AVOutputFormat *, AVCodecList>::iterator it = formatList.begin();
        for (; it != formatList.end(); it++)
        {
            myPlugin->selectFormat->addEntry((*it).first->long_name);
            int length = strlen((*it).first->long_name);
            coVRMSController::instance()->sendSlaves((char *)&length, sizeof(length));
            coVRMSController::instance()->sendSlaves((*it).first->long_name, length + 1);
        }
    }
    else
    {
        for (int i = 0; i < count; i++)
        {
            int length;
            coVRMSController::instance()->readMaster((char *)&length, sizeof(length));
            char *charString = new char[length + 1];
            coVRMSController::instance()->readMaster(charString, length + 1);
            myPlugin->selectFormat->addEntry(string(charString));
            delete[] charString;
        }
    }

    if (count != 0)
        FillComboBoxSetExtension(0, row);

    myPlugin->helpText = "The video plugin offers file formats installed. Most suitable codecs for a selected format are "
                         "listed "
                         "first.\n The bitrates depend on the complexity and the size of the image and should be chosen as "
                         "small as possible to get an appropriate quality. The following list shows some suitable values for "
                         "the average and the maximum bitrate:\n\n MPEG-4 and H.264 formats:\n 1920x1080 24 MBits 30MBits\n "
                         "720x576 3 MBits 6 MBits\n 1440x1080 15 MBits 18 MBits\n 320x240 0,4 MBits 0,6 MBits\n\n WindowsMedia "
                         "formats:\n 1920x1080 8000 Kbits 10000 Kbits\n 1280x720  5000 Kbits 8000 Kbits\n 720x576 1500 KBits "
                         "6000 Kbits\n\n The parameter dataset file (videoparams.xml) contains the dimensions, the bitrates "
                         "and "
                         "the frame rate and is located in .covise.\n\n [ALT]g is the shortcut to start capturing in the "
                         "OpenCover window.";
    myPlugin->messageBox->setText(myPlugin->helpText);

    int size = 0;
    if (coVRMSController::instance()->isMaster())
    {
        size = readParams();
        sendParams();
    }
    else
    {
        size = getParams();
    }
    if (size > 0)
    {
        loadParams(VPList.size() - 1);
    }
    fillParamComboBox(row + 1);
    selectParams->setSelectedEntry(VPList.size());
}

void FFMPEGPlugin::changeFormat(coTUIElement *tUIItem, int row)
{
    if (selectCodec)
    {
        delete selectCodec;
        selectCodec = NULL;
    }
    FillComboBoxSetExtension(myPlugin->selectFormat->getSelectedEntry(), row);
}

void FFMPEGPlugin::tabletEvent(coTUIElement *tUIItem)
{
    if (tUIItem == selectCodec)
    {
        const char *codecName, *formatName;
        std::map<AVOutputFormat *, AVCodecList>::iterator it = formatList.begin();
        int formatEntry = myPlugin->selectFormat->getSelectedEntry();
        if (formatEntry < 0)
            formatName = (*formatList.begin()).first->long_name;
        else
        {
            formatName = myPlugin->selectFormat->getSelectedText().c_str();
            for (int i = 0; i < formatEntry; i++)
                it++;
        }

        if (selectCodec->getSelectedEntry() < 0)
            codecName = (*(*it).second.begin())->long_name;
        else
            codecName = selectCodec->getSelectedText().c_str();

        if (strcmp(codecName, "DV (Digital Video)") == 0)
            hideParamMenu(true);
        else
            hideParamMenu(false);
    }
    else if (tUIItem == saveButton)
    {
        addParams();
        if (coVRMSController::instance()->isMaster())
            saveParams();
        saveButton->setState(false);
    }
    else if (tUIItem == selectParams)
    {
        loadParams(selectParams->getSelectedEntry());
    }
}

void FFMPEGPlugin::loadParams(int select)
{
    std::list<VideoParameter>::iterator it = VPList.begin();
    for (int i = 0; i < select; i++)
        it++;
    VideoParameter VP = (*it);

    paramNameField->setText(VP.name);
    myPlugin->outWidth = VP.width;
    myPlugin->outWidthField->setValue(myPlugin->outWidth);
    myPlugin->outHeight = VP.height;
    myPlugin->outHeightField->setValue(myPlugin->outHeight);
    if (VP.fps == "Constant")
    {
        myPlugin->selectFrameRate->setSelectedEntry(2);
        myPlugin->time_base = VP.constFrames;
        myPlugin->cfpsEdit->setValue(myPlugin->time_base);
        myPlugin->cfpsHide(false);
    }
    else
    {
        myPlugin->selectFrameRate->setSelectedText(VP.fps);
        sscanf(VP.fps.c_str(), "%d", &myPlugin->time_base);
        myPlugin->cfpsHide(true);
    }
    bitrateField->setValue(VP.avgBitrate);
    maxBitrateField->setValue(VP.maxBitrate);
}

int FFMPEGPlugin::readParams()
{
    QString path = coConfigDefaultPaths::getDefaultLocalConfigFilePath();
    std::string pathname = path.toStdString();
    pathname += "videoparams.xml";
    xercesc::XercesDOMParser *parser = new xercesc::XercesDOMParser();
    parser->setValidationScheme(xercesc::XercesDOMParser::Val_Never);

    try
    {
        parser->parse(pathname.c_str());
    }
    catch (...)
    {
        cerr << "error parsing param table" << endl;
    }

    xercesc::DOMDocument *xmlDoc = parser->getDocument();
    xercesc::DOMElement *rootElement = NULL;
    if (xmlDoc)
    {
        rootElement = xmlDoc->getDocumentElement();
    }

    if (rootElement)
    {
        xercesc::DOMNodeList *nodeList = rootElement->getChildNodes();
        for (int i = 0; i < nodeList->getLength(); ++i)
        {
            xercesc::DOMElement *node = dynamic_cast<xercesc::DOMElement *>(nodeList->item(i));
            if (!node)
                continue;
            VideoParameter VP;
            VP.name = xercesc::XMLString::transcode(node->getAttribute(xercesc::XMLString::transcode("name")));
            xercesc::DOMNodeList *childList = node->getChildNodes();
            for (int j = 0; j < childList->getLength(); j++)
            {
                xercesc::DOMElement *child = dynamic_cast<xercesc::DOMElement *>(childList->item(j));
                if (!child)
                    continue;

                const char *w = xercesc::XMLString::transcode(child->getAttribute(xercesc::XMLString::transcode("outWidth")));
                const char *h = xercesc::XMLString::transcode(child->getAttribute(xercesc::XMLString::transcode("outHeight")));
                VP.fps = xercesc::XMLString::transcode(child->getAttribute(xercesc::XMLString::transcode("frameRate")));
                const char *constFrameRate = xercesc::XMLString::transcode(
                    child->getAttribute(xercesc::XMLString::transcode("constantFrameRate")));
                const char *avgBitrate = xercesc::XMLString::transcode(
                    child->getAttribute(xercesc::XMLString::transcode("bitrateAverage")));
                const char *maxBitrate = xercesc::XMLString::transcode(
                    child->getAttribute(xercesc::XMLString::transcode("bitrateMax")));

                sscanf(w, "%d", &VP.width);
                sscanf(h, "%d", &VP.height);
                if (VP.fps == "Constant")
                {
                    sscanf(constFrameRate, "%d", &VP.constFrames);
                }
                else
                    VP.constFrames = 0;

                sscanf(avgBitrate, "%d", &VP.avgBitrate);
                sscanf(maxBitrate, "%d", &VP.maxBitrate);

                VPList.push_back(VP);
            }
        }
    }

    return VPList.size();
}

void FFMPEGPlugin::saveParams()
{
    QString path = coConfigDefaultPaths::getDefaultLocalConfigFilePath();
    std::string pathname = path.toStdString();
    pathname += "videoparams.xml";

    xercesc::DOMDocument *xmlDoc = impl->createDocument(0, xercesc::XMLString::transcode("VideoParams"), 0);
    xercesc::DOMElement *rootElement = NULL;
    if (xmlDoc)
    {
        rootElement = xmlDoc->getDocumentElement();
    }

    if (rootElement)
    {
        std::list<VideoParameter>::iterator it;
        for (it = VPList.begin(); it != VPList.end(); it++)
        {
            xercesc::DOMElement *VPElement = xmlDoc->createElement(xercesc::XMLString::transcode("VPEntry"));
            VPElement->setAttribute(xercesc::XMLString::transcode("name"),
                                    xercesc::XMLString::transcode((*it).name.c_str()));
            xercesc::DOMElement *VPChild = xmlDoc->createElement(xercesc::XMLString::transcode("VPValues"));
            char nr[100];
            sprintf(nr, "%d", (*it).width);
            VPChild->setAttribute(xercesc::XMLString::transcode("outWidth"), xercesc::XMLString::transcode(nr));
            sprintf(nr, "%d", (*it).height);
            VPChild->setAttribute(xercesc::XMLString::transcode("outHeight"), xercesc::XMLString::transcode(nr));
            VPChild->setAttribute(xercesc::XMLString::transcode("frameRate"),
                                  xercesc::XMLString::transcode((*it).fps.c_str()));
            sprintf(nr, "%d", (*it).constFrames);
            VPChild->setAttribute(xercesc::XMLString::transcode("constantFrameRate"),
                                  xercesc::XMLString::transcode(nr));
            sprintf(nr, "%d", (*it).maxBitrate);
            VPChild->setAttribute(xercesc::XMLString::transcode("bitrateMax"),
                                  xercesc::XMLString::transcode(nr));
            sprintf(nr, "%d", (*it).avgBitrate);
            VPChild->setAttribute(xercesc::XMLString::transcode("bitrateAverage"),
                                  xercesc::XMLString::transcode(nr));

            VPElement->appendChild(VPChild);
            rootElement->appendChild(VPElement);
        }

#if XERCES_VERSION_MAJOR < 3
        xercesc::DOMWriter *writer = impl->createDOMWriter();
        // set the format-pretty-print feature
        if (writer->canSetFeature(xercesc::XMLUni::fgDOMWRTFormatPrettyPrint, true))
            writer->setFeature(xercesc::XMLUni::fgDOMWRTFormatPrettyPrint, true);
        xercesc::XMLFormatTarget *xmlTarget = new xercesc::LocalFileFormatTarget(pathname.c_str());
        bool written = writer->writeNode(xmlTarget, *rootElement);
        if (!written)
            fprintf(stderr, "VideoParams::save info: Could not open file for writing !\n");

        delete writer;
        delete xmlTarget;
#else

        xercesc::DOMLSSerializer *writer = ((xercesc::DOMImplementationLS *)impl)->createLSSerializer();

        xercesc::DOMLSOutput *theOutput = ((xercesc::DOMImplementationLS *)impl)->createLSOutput();
        theOutput->setEncoding(xercesc::XMLString::transcode("utf8"));

        bool written = writer->writeToURI(rootElement, xercesc::XMLString::transcode(pathname.c_str()));
        if (!written)
            fprintf(stderr, "save info: Could not open file for writing %s!\n", pathname.c_str());
        delete writer;

#endif
    }
    delete xmlDoc;
}

void FFMPEGPlugin::addParams()
{
    VideoParameter VP;
    static bool overwrite = false;
    static std::string oldName = "";

    std::list<VideoParameter>::iterator it;
    int pos = 0;
    myPlugin->errorLabel->setLabel("");
    for (it = VPList.begin(); it != VPList.end(); it++)
    {
        if ((*it).name == paramNameField->getText())
        {
            if (overwrite && (oldName == paramNameField->getText()))
            {
                break;
            }
            else
            {
                char buf[1000];
                sprintf(buf, "Parameter name already in List. Press button again to overwrite.");
                myPlugin->errorLabel->setLabel(buf);
                myPlugin->sizeError = true;
                overwrite = true;
                oldName = paramNameField->getText();

                return;
            }
        }
        else
            pos++;
    }
    if (it == VPList.end())
    {
        if (overwrite)
            overwrite = false;
    }

    VP.name = paramNameField->getText();
    VP.width = myPlugin->outWidthField->getValue();
    VP.height = myPlugin->outHeightField->getValue();
    VP.fps = myPlugin->selectFrameRate->getSelectedText();
    VP.constFrames = myPlugin->cfpsEdit->getValue();
    VP.maxBitrate = maxBitrateField->getValue();
    VP.avgBitrate = bitrateField->getValue();

    if (!overwrite)
    {
        VPList.push_back(VP);
        selectParams->addEntry(VP.name);
    }
    else
    {
        (*it) = VP;
        overwrite = false;
    }

    selectParams->setSelectedEntry(pos);
}

void FFMPEGPlugin::sendParams()
{
    std::list<VideoParameter>::iterator it;
    int listSize = VPList.size();
    coVRMSController::instance()->sendSlaves((char *)&listSize, sizeof(int));
    for (it = VPList.begin(); it != VPList.end(); it++)
    {
        int length = strlen((*it).name.c_str());
        coVRMSController::instance()->sendSlaves((char *)&length, sizeof(int));
        coVRMSController::instance()->sendSlaves((*it).name.c_str(), length + 1);
        coVRMSController::instance()->sendSlaves((char *)&(*it).width, sizeof(int));
        coVRMSController::instance()->sendSlaves((char *)&(*it).height, sizeof(int));
        length = strlen((*it).fps.c_str());
        coVRMSController::instance()->sendSlaves((char *)&length, sizeof(length));
        coVRMSController::instance()->sendSlaves((*it).fps.c_str(), length + 1);
        coVRMSController::instance()->sendSlaves((char *)&(*it).constFrames, sizeof(int));
        coVRMSController::instance()->sendSlaves((char *)&(*it).maxBitrate, sizeof(int));
        coVRMSController::instance()->sendSlaves((char *)&(*it).avgBitrate, sizeof(int));
    }
}

int FFMPEGPlugin::getParams()
{
    int listSize = 0;

    coVRMSController::instance()->readMaster((char *)&listSize, sizeof(int));
    for (int i = 1; i <= listSize; i++)
    {
        VideoParameter VP;
        int length;
        coVRMSController::instance()->readMaster((char *)&length, sizeof(int));
        char *charString = new char[length + 1];
        coVRMSController::instance()->readMaster(charString, length + 1);
        VP.name = charString;
        delete[] charString;

        coVRMSController::instance()->readMaster((char *)&VP.width, sizeof(int));
        coVRMSController::instance()->readMaster((char *)&VP.height, sizeof(int));
        coVRMSController::instance()->readMaster((char *)&length, sizeof(int));
        charString = new char[length + 1];
        coVRMSController::instance()->readMaster(charString, length + 1);
        VP.fps = charString;
        delete[] charString;

        coVRMSController::instance()->readMaster((char *)&VP.constFrames, sizeof(int));
        coVRMSController::instance()->readMaster((char *)&VP.maxBitrate, sizeof(int));
        coVRMSController::instance()->readMaster((char *)&VP.avgBitrate, sizeof(int));

        VPList.push_back(VP);
    }

    return VPList.size();
}
