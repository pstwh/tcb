#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"
#include "whisper.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sndfile.h>

#define RECORD_FOLDER "tcb"
#define BUFFER_SIZE_IN_FRAMES 1024 * 16

#define TARGET_FORMAT ma_format_f32
#define TARGET_CHANNELS 1
#define TARGET_SAMPLE_RATE 16000

struct mix_context
{
    ma_pcm_rb *rbPrimary;
    ma_pcm_rb *rbSecundary;
    ma_data_converter *converterPrimary;
    ma_data_converter *converterSecundary;
    ma_encoder *encoder;
};

void ensure_record_folder()
{
    char *home = getenv("HOME");
    if (home == NULL)
    {
        printf("Failed to get HOME directory.\n");
        exit(1);
    }

    char folder_path[512];
    snprintf(folder_path, sizeof(folder_path), "%s/%s", home, RECORD_FOLDER);

    struct stat st;
    if (stat(folder_path, &st) == -1)
    {
        if (mkdir(folder_path, 0700) != 0)
        {
            printf("Failed to create folder: %s\n", folder_path);
            exit(1);
        }
    }
}

void list_devices(ma_context *context)
{
    ma_device_info *pPlaybackDeviceInfos;
    ma_uint32 playbackDeviceCount;
    ma_device_info *pCaptureDeviceInfos;
    ma_uint32 captureDeviceCount;

    if (ma_context_get_devices(context, &pPlaybackDeviceInfos, &playbackDeviceCount, &pCaptureDeviceInfos, &captureDeviceCount) != MA_SUCCESS)
    {
        printf("Failed to retrieve device information.\n");
        return;
    }

    printf("Playback Devices:\n");
    for (ma_uint32 i = 0; i < playbackDeviceCount; ++i)
    {
        printf("    %u: %s\n", i, pPlaybackDeviceInfos[i].name);
    }

    printf("\nCapture Devices:\n");
    for (ma_uint32 i = 0; i < captureDeviceCount; ++i)
    {
        printf("    %u: %s\n", i, pCaptureDeviceInfos[i].name);
    }
}

void list_records()
{
    char *home = getenv("HOME");
    char folder_path[512];
    snprintf(folder_path, sizeof(folder_path), "%s/%s", home, RECORD_FOLDER);

    DIR *dir = opendir(folder_path);
    if (dir == NULL)
    {
        printf("Failed to open record folder: %s\n", folder_path);
        return;
    }

    printf("Available Records:\n");
    struct dirent *entry;
    int index = 0;
    while ((entry = readdir(dir)) != NULL)
    {
        // if (entry->d_type == DT_REG) {
        printf("    %d: %s\n", index, entry->d_name);
        index++;
        // }
    }

    closedir(dir);
}

void play_record(const char *record_name)
{
    char *home = getenv("HOME");
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/%s/%s", home, RECORD_FOLDER, record_name);

    ma_decoder decoder;
    if (ma_decoder_init_file(file_path, NULL, &decoder) != MA_SUCCESS)
    {
        printf("Failed to initialize decoder for file: %s\n", file_path);
        return;
    }

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = decoder.outputFormat;
    deviceConfig.playback.channels = decoder.outputChannels;
    deviceConfig.sampleRate = decoder.outputSampleRate;
    // deviceConfig.dataCallback = ma_data_callback;
    deviceConfig.pUserData = &decoder;

    ma_device device;
    if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS)
    {
        printf("Failed to initialize playback device.\n");
        ma_decoder_uninit(&decoder);
        return;
    }

    if (ma_device_start(&device) != MA_SUCCESS)
    {
        printf("Failed to start playback device.\n");
        ma_device_uninit(&device);
        ma_decoder_uninit(&decoder);
        return;
    }

    printf("Playing record: %s\n", record_name);
    printf("Press Enter to stop playback...\n");
    getchar();

    ma_device_uninit(&device);
    ma_decoder_uninit(&decoder);
}

void rb_write_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
    ma_uint32 framesToWrite = frameCount;
    ma_uint32 bytesPerFrame = ma_get_bytes_per_frame(pDevice->capture.format,
                                                     pDevice->capture.channels);
    ma_pcm_rb *rb = (ma_pcm_rb *)pDevice->pUserData;
    MA_ASSERT(rb != NULL);

    void *rbWrite;
    if (ma_pcm_rb_acquire_write(rb, &framesToWrite, &rbWrite) != MA_SUCCESS)
    {
        printf("Failed to acquire write buffer.\n");
        return;
    }

    memcpy(rbWrite, pInput, framesToWrite * bytesPerFrame);
    if (ma_pcm_rb_commit_write(rb, framesToWrite) != MA_SUCCESS)
    {
        printf("Failed to commit write buffer.\n");
    }
    else
    {
        // printf("(WRITE) Committed %u frames.\n", framesToWrite);
    }
}

void *rb_read_thread(void *arg)
{
    struct mix_context *mixContext = (struct mix_context *)arg;
    assert(mixContext != NULL);

    ma_uint32 frameCountPrimary;
    ma_uint32 frameCountSecundary;
    ma_uint32 frameCount;
    ma_uint32 bytesPerFrameNew = ma_get_bytes_per_frame(TARGET_FORMAT, TARGET_CHANNELS);
    ma_pcm_rb *rb = mixContext->rbPrimary;
    ma_pcm_rb *rbSecundary = mixContext->rbSecundary;
    ma_data_converter *converterPrimary = mixContext->converterPrimary;
    ma_data_converter *converterSecundary = mixContext->converterSecundary;
    ma_encoder *encoder = mixContext->encoder;

    assert(encoder != NULL);

    void *rbRead = NULL;
    void *rbOtherRead = NULL;

    while (1)
    {
        frameCountPrimary = ma_pcm_rb_available_read(rb);
        frameCountSecundary = ma_pcm_rb_available_read(rbSecundary);

        frameCount = ma_min(frameCountPrimary, frameCountSecundary);
        if (frameCount > 0)
        {
            ma_result rbPrimaryResult = ma_pcm_rb_acquire_read(rb, &frameCount, &rbRead);
            ma_result rbSecundaryResult = ma_pcm_rb_acquire_read(rbSecundary, &frameCount, &rbOtherRead);

            if (rbPrimaryResult == MA_SUCCESS && rbSecundaryResult == MA_SUCCESS)
            {
                ma_int16 *rbBuffer = (ma_int16 *)rbRead;
                ma_int16 *rbOtherBuffer = (ma_int16 *)rbOtherRead;

                ma_uint64 frameCountOld = frameCount;
                ma_uint64 frameCountConvertedPrimary;
                ma_uint64 frameCountConvertedSecundary;
                if (ma_data_converter_get_expected_output_frame_count(converterPrimary, frameCountOld, &frameCountConvertedPrimary) != MA_SUCCESS)
                {
                    printf("Failed to get expected output frame count.\n");
                }
                if (ma_data_converter_get_expected_output_frame_count(converterSecundary, frameCountOld, &frameCountConvertedSecundary) != MA_SUCCESS)
                {
                    printf("Failed to get expected output frame count.\n");
                }

                ma_uint64 frameCountConverted = ma_min(frameCountConvertedPrimary, frameCountConvertedSecundary);

                ma_float *rbBufferConverted = malloc(frameCountConverted * bytesPerFrameNew);
                ma_float *rbOtherBufferConverted = malloc(frameCountConverted * bytesPerFrameNew);
                ma_uint64 framesConvertedPrimary = frameCountConverted;
                ma_uint64 framesConvertedSecundary = frameCountConverted;
                if (ma_data_converter_process_pcm_frames(converterPrimary, rbBuffer, &frameCountOld, rbBufferConverted, &framesConvertedPrimary) != MA_SUCCESS)
                {
                    printf("Failed to convert primary buffer.\n");
                }

                frameCountOld = frameCount;
                if (ma_data_converter_process_pcm_frames(converterSecundary, rbOtherBuffer, &frameCountOld, rbOtherBufferConverted, &framesConvertedSecundary) != MA_SUCCESS)
                {
                    printf("Failed to convert secondary buffer.\n");
                }

                for (ma_uint32 i = 0; i < frameCountConverted; i++)
                {
                    rbBufferConverted[i] = ma_clamp((rbBufferConverted[i] + rbOtherBufferConverted[i]) / 2.0f, -1.0f, 1.0f);
                }

                ma_uint64 framesWritten;
                if (ma_encoder_write_pcm_frames(encoder, rbBufferConverted, frameCountConverted, &framesWritten) != MA_SUCCESS)
                {
                    printf("Failed to write to encoder.\n");
                }

                ma_pcm_rb_commit_read(rb, frameCount);
                ma_pcm_rb_commit_read(rbSecundary, frameCount);

                free(rbBufferConverted);
                free(rbOtherBufferConverted);
            }
        }

        sleep(0.5);
    }

    return NULL;
}

int main(int argc, char **argv)
{
    struct whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = true;
    cparams.flash_attn = true;
    cparams.dtw_aheads_preset = WHISPER_AHEADS_LARGE_V3_TURBO;
    struct whisper_context *ctx = whisper_init_from_file_with_params("/home/pstwh/tcb/ggml-large-v3-turbo-q5_0.bin", cparams);
    if (!ctx)
    {
        printf("Failed to initialize whisper context.\n");
        return 1;
    }

    ensure_record_folder();

    ma_context context;
    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS)
    {
        printf("Failed to initialize context.\n");
        return -1;
    }

    if (argc < 2)
    {
        printf("Usage: %s <command> [options]\n", argv[0]);
        printf("Commands:\n");
        printf("    list-devices            List available devices\n");
        printf("    list-records            List all recorded files\n");
        printf("    play <name/number>      Play a specific record\n");
        printf("    record <dev1> <dev2>   Record using specified devices\n");
        return 0;
    }

    if (strcmp(argv[1], "list-devices") == 0)
    {
        list_devices(&context);
    }
    else if (strcmp(argv[1], "list-records") == 0)
    {
        list_records();
    }
    else if (strcmp(argv[1], "play") == 0)
    {
        if (argc < 3)
        {
            printf("Specify the record name or number to play.\n");
            return -1;
        }
        play_record(argv[2]);
    }
    else if (strcmp(argv[1], "record") == 0)
    {
        if (argc < 4)
        {
            printf("Specify primary and secondary device IDs for recording.\n");
            return -1;
        }

        int primary_device = atoi(argv[2]);
        int secondary_device = atoi(argv[3]);

        char file_path[512];
        char *home = getenv("HOME");

        char *file_prefix = "tcp";
        char *language = "pt";
        for (int i = 4; i < argc; i++)
        {
            if (strcmp(argv[i], "--record-name") == 0 && i + 1 < argc)
            {
                file_prefix = argv[i + 1];
                break;
            }

            if (strcmp(argv[i], "--language") == 0 && i + 1 < argc)
            {
                language = argv[i + 1];
                break;
            }
        }

        time_t now = time(NULL);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));

        snprintf(file_path, sizeof(file_path), "%s/%s/%s_%s.wav", home, RECORD_FOLDER, file_prefix, timestamp);

        printf("Recording to file: %s\n", file_path);

        ma_context context;
        if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS)
        {
            printf("Failed to initialize context.\n");
            return -2;
        }

        ma_device_info *pPlaybackDeviceInfos;
        ma_uint32 playbackDeviceCount;
        ma_device_info *pCaptureDeviceInfos;
        ma_uint32 captureDeviceCount;
        if (ma_context_get_devices(&context, &pPlaybackDeviceInfos, &playbackDeviceCount, &pCaptureDeviceInfos, &captureDeviceCount) != MA_SUCCESS)
        {
            printf("Failed to retrieve device information.\n");
            return -3;
        }

        ma_device_config deviceConfigPrimary = ma_device_config_init(ma_device_type_capture);
        deviceConfigPrimary.capture.pDeviceID = &pCaptureDeviceInfos[primary_device].id;
        deviceConfigPrimary.dataCallback = rb_write_callback;

        ma_device devicePrimary;
        if (ma_device_init(NULL, &deviceConfigPrimary, &devicePrimary) != MA_SUCCESS)
        {
            printf("Failed to initialize capture device.\n");
            return -2;
        }

        ma_pcm_rb rbPrimary;
        ma_result result = ma_pcm_rb_init(devicePrimary.capture.format, devicePrimary.capture.channels, BUFFER_SIZE_IN_FRAMES, NULL, NULL, &rbPrimary);
        if (result != MA_SUCCESS)
        {
            printf("Failed to initialize ring buffer.\n");
            return -1;
        }

        devicePrimary.pUserData = &rbPrimary;

        ma_data_converter_config converterPrimaryConfig = ma_data_converter_config_init(
            devicePrimary.capture.format,
            TARGET_FORMAT,
            devicePrimary.capture.channels,
            TARGET_CHANNELS,
            devicePrimary.sampleRate,
            TARGET_SAMPLE_RATE);

        ma_data_converter converterPrimary;
        if (ma_data_converter_init(&converterPrimaryConfig, NULL, &converterPrimary) != MA_SUCCESS)
        {
            printf("Failed to initialize data converter.\n");
        }

        if (ma_device_start(&devicePrimary) != MA_SUCCESS)
        {
            ma_device_uninit(&devicePrimary);
            printf("Failed to start device.\n");
            return -3;
        }

        ma_device_config deviceConfigSecundary = ma_device_config_init(ma_device_type_capture);
        deviceConfigSecundary.capture.pDeviceID = &pCaptureDeviceInfos[secondary_device].id;
        deviceConfigSecundary.dataCallback = rb_write_callback;

        ma_device deviceSecundary;
        if (ma_device_init(NULL, &deviceConfigSecundary, &deviceSecundary) != MA_SUCCESS)
        {
            printf("Failed to initialize capture device.\n");
            return -2;
        }

        ma_pcm_rb rbSecundary;
        result = ma_pcm_rb_init(deviceSecundary.capture.format, deviceSecundary.capture.channels, BUFFER_SIZE_IN_FRAMES, NULL, NULL, &rbSecundary);
        if (result != MA_SUCCESS)
        {
            printf("Failed to initialize ring buffer.\n");
            return -1;
        }

        deviceSecundary.pUserData = &rbSecundary;
        ma_data_converter_config converterSecundaryConfig = ma_data_converter_config_init(
            deviceSecundary.capture.format,
            TARGET_FORMAT,
            deviceSecundary.capture.channels,
            TARGET_CHANNELS,
            deviceSecundary.sampleRate,
            TARGET_SAMPLE_RATE);

        ma_data_converter converterSecundary;
        if (ma_data_converter_init(&converterSecundaryConfig, NULL, &converterSecundary) != MA_SUCCESS)
        {
            printf("Failed to initialize data converter.\n");
        }

        if (ma_device_start(&devicePrimary) != MA_SUCCESS)
        {
            ma_device_uninit(&devicePrimary);
            printf("Failed to start device.\n");
            return -3;
        }

        if (ma_device_start(&deviceSecundary) != MA_SUCCESS)
        {
            ma_device_uninit(&deviceSecundary);
            printf("Failed to start device.\n");
            return -3;
        }

        ma_encoder_config encoderConfig = ma_encoder_config_init(
            ma_encoding_format_wav,
            TARGET_FORMAT,
            TARGET_CHANNELS,
            TARGET_SAMPLE_RATE);

        ma_encoder encoder;
        if (ma_encoder_init_file(file_path, &encoderConfig, &encoder) != MA_SUCCESS)
        {
            printf("Failed to initialize output file.\n");
        }

        struct mix_context rbs = {.rbPrimary = &rbPrimary, .rbSecundary = &rbSecundary, .encoder = &encoder, .converterPrimary = &converterPrimary, .converterSecundary = &converterSecundary};

        pthread_t thread;
        pthread_create(&thread, NULL, rb_read_thread, (void *)(&rbs));

        printf("Press Enter to stop recording...\n");
        getchar();

        ma_encoder_uninit(&encoder);
        ma_device_uninit(&devicePrimary);
        ma_device_uninit(&deviceSecundary);
        pthread_cancel(thread);

        ma_context_uninit(&context);

        printf("%s\n", file_path);

        ma_decoder_config decoder_config = ma_decoder_config_init(TARGET_FORMAT, TARGET_CHANNELS, TARGET_SAMPLE_RATE);

        ma_decoder decoder;
        if (ma_decoder_init_file(file_path, &decoder_config, &decoder) != MA_SUCCESS)
        {
            printf("Failed to initialize decoder for file: %s\n", file_path);
        }

        ma_uint64 framesSize;
        if (ma_decoder_get_length_in_pcm_frames(&decoder, &framesSize) != MA_SUCCESS)
        {
            printf("Failed to get length of file.\n");
        }

        ma_uint32 bytesPerFrameNew = ma_get_bytes_per_frame(TARGET_FORMAT, TARGET_CHANNELS);
        ma_uint64 bufferSize = framesSize * bytesPerFrameNew;

        ma_float *audioBuffer = malloc(bufferSize);
        ma_uint64 framesRead;
        if (ma_decoder_read_pcm_frames(&decoder, audioBuffer, framesSize, &framesRead) != MA_SUCCESS)
        {
            printf("Failed to read audio data. %d\n", result);
        }

        struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        wparams.language = "pt";
        wparams.n_threads = 4;
        wparams.strategy = WHISPER_SAMPLING_BEAM_SEARCH;
        wparams.beam_search.beam_size = 5;

        if (whisper_full_parallel(ctx, wparams, audioBuffer, framesSize, 1) != 0)
        {
            fprintf(stderr, "Failed to process audio\n");
            free(audioBuffer);
            whisper_free(ctx);
            return 1;
        }

        const int segments = whisper_full_n_segments(ctx);
        for (int i = 0; i < segments; i++)
        {
            const char *text = whisper_full_get_segment_text(ctx, i);
            printf("Segment %d: %s\n", i, text);
        }

        free(audioBuffer);
        whisper_free(ctx);
    }
    else
    {
        printf("Unknown command: %s\n", argv[1]);
    }

    ma_context_uninit(&context);
    return 0;
}
