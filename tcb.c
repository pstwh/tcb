#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#define FORMAT ma_format_s16
#define CHANNELS 2
#define BUFFER_SIZE_IN_FRAMES 1024 * 16
#define SAMPLE_RATE 44100
#define RECORD_FOLDER "tcb"

struct mix_context
{
    ma_pcm_rb *rbPrimary;
    ma_pcm_rb *rbSecondary;
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

void rb_read_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{

    struct mix_context *mixContext = (struct mix_context *)pDevice->pUserData;
    MA_ASSERT(mixContext != NULL);

    ma_uint32 framesToRead = frameCount;
    ma_uint32 bytesPerFrame = ma_get_bytes_per_frame(pDevice->playback.format,
                                                     pDevice->playback.channels);
    ma_pcm_rb *rb = mixContext->rbPrimary;
    ma_pcm_rb *rbSecondary = mixContext->rbSecondary;
    ma_encoder *encoder = mixContext->encoder;

    MA_ASSERT(encoder != NULL);

    void *rbRead = NULL;
    void *rbOtherRead = NULL;

    ma_result rbPrimaryResult = ma_pcm_rb_acquire_read(rb, &frameCount, &rbRead);
    ma_result rbSecondaryResult = ma_pcm_rb_acquire_read(rbSecondary, &frameCount, &rbOtherRead);

    int16_t *mixedBuffer = malloc(frameCount * bytesPerFrame);
    if (mixedBuffer == NULL)
    {
        printf("Memory allocation failed for mixed buffer.\n");
        return;
    }

    if (rbPrimaryResult == MA_SUCCESS && rbSecondaryResult == MA_SUCCESS)
    {
        int16_t *rbBuffer = (int16_t *)rbRead;
        int16_t *rbOtherBuffer = (int16_t *)rbOtherRead;

        for (ma_uint32 i = 0; i < frameCount * pDevice->playback.channels; i++)
        {
            int32_t mixedSample = ((int32_t)rbBuffer[i] + (int32_t)rbOtherBuffer[i]) / 2;

            mixedBuffer[i] = (int16_t)ma_clamp(mixedSample, -32768, 32767);
        }

        ma_encoder_write_pcm_frames(encoder, mixedBuffer, frameCount, NULL);

        ma_pcm_rb_commit_read(rb, frameCount);
        ma_pcm_rb_commit_read(rbSecondary, frameCount);

        // printf("Encoded %u mixed frames.\n", frameCount);
    }
    else if (rbPrimaryResult == MA_SUCCESS)
    {

        ma_encoder_write_pcm_frames(encoder, rbRead, frameCount, NULL);
        ma_pcm_rb_commit_read(rb, frameCount);

        // printf("Encoded %u frames from first buffer.\n", frameCount);
    }
    else if (rbSecondaryResult == MA_SUCCESS)
    {

        ma_encoder_write_pcm_frames(encoder, rbOtherRead, frameCount, NULL);
        ma_pcm_rb_commit_read(rbSecondary, frameCount);

        // printf("Encoded %u frames from second buffer.\n", frameCount);
    }
    else
    {
        // printf("No data available in either buffer.\n");
    }

    free(mixedBuffer);
}

int main(int argc, char **argv)
{
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

        time_t now = time(NULL);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));

        char *home = getenv("HOME");
        char file_path[512];
        snprintf(file_path, sizeof(file_path), "%s/%s/tcb_%s.wav", home, RECORD_FOLDER, timestamp);

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

        ma_pcm_rb rbPrimary;
        ma_result result = ma_pcm_rb_init(FORMAT, CHANNELS, BUFFER_SIZE_IN_FRAMES, NULL, NULL, &rbPrimary);
        if (result != MA_SUCCESS)
        {
            printf("Failed to initialize ring buffer.\n");
            return -1;
        }

        ma_device_config deviceConfigPrimary = ma_device_config_init(ma_device_type_capture);
        deviceConfigPrimary.capture.pDeviceID = &pCaptureDeviceInfos[1].id;
        deviceConfigPrimary.capture.format = FORMAT;
        deviceConfigPrimary.capture.channels = CHANNELS;
        deviceConfigPrimary.sampleRate = SAMPLE_RATE;
        deviceConfigPrimary.dataCallback = rb_write_callback;
        deviceConfigPrimary.pUserData = &rbPrimary;

        ma_device devicePrimary;
        if (ma_device_init(NULL, &deviceConfigPrimary, &devicePrimary) != MA_SUCCESS)
        {
            printf("Failed to initialize capture device.\n");
            return -2;
        }

        if (ma_device_start(&devicePrimary) != MA_SUCCESS)
        {
            ma_device_uninit(&devicePrimary);
            printf("Failed to start device.\n");
            return -3;
        }

        ma_pcm_rb rbSecondary;
        result = ma_pcm_rb_init(FORMAT, CHANNELS, BUFFER_SIZE_IN_FRAMES, NULL, NULL, &rbSecondary);
        if (result != MA_SUCCESS)
        {
            printf("Failed to initialize ring buffer.\n");
            return -1;
        }

        ma_device_config deviceConfigSecundary = ma_device_config_init(ma_device_type_capture);
        deviceConfigSecundary.capture.pDeviceID = &pCaptureDeviceInfos[2].id;
        deviceConfigSecundary.capture.format = FORMAT;
        deviceConfigSecundary.capture.channels = CHANNELS;
        deviceConfigSecundary.sampleRate = SAMPLE_RATE;
        deviceConfigSecundary.dataCallback = rb_write_callback;
        deviceConfigSecundary.pUserData = &rbSecondary;

        ma_device deviceSecundary;
        if (ma_device_init(NULL, &deviceConfigSecundary, &deviceSecundary) != MA_SUCCESS)
        {
            printf("Failed to initialize capture device.\n");
            return -2;
        }

        if (ma_device_start(&deviceSecundary) != MA_SUCCESS)
        {
            ma_device_uninit(&deviceSecundary);
            printf("Failed to start device.\n");
            return -3;
        }

        ma_encoder_config encoderConfig = ma_encoder_config_init(
            ma_encoding_format_wav,
            FORMAT,
            CHANNELS,
            SAMPLE_RATE);

        ma_encoder encoder;
        if (ma_encoder_init_file(file_path, &encoderConfig, &encoder) != MA_SUCCESS)
        {
            printf("Failed to initialize output file.\n");
        }

        struct mix_context rbs = {.rbPrimary = &rbPrimary, .rbSecondary = &rbSecondary, .encoder = &encoder};

        ma_device_config deviceConfigAux = ma_device_config_init(ma_device_type_playback);
        deviceConfigAux.playback.pDeviceID = &pPlaybackDeviceInfos[1].id;
        deviceConfigAux.playback.format = FORMAT;
        deviceConfigAux.playback.channels = CHANNELS;
        deviceConfigAux.sampleRate = SAMPLE_RATE;
        deviceConfigAux.dataCallback = rb_read_callback;
        deviceConfigAux.pUserData = &rbs;

        ma_device deviceAux;
        if (ma_device_init(NULL, &deviceConfigAux, &deviceAux) != MA_SUCCESS)
        {
            printf("Failed to initialize playback device.\n");
            return -2;
        }

        if (ma_device_start(&deviceAux) != MA_SUCCESS)
        {
            ma_device_uninit(&deviceAux);
            printf("Failed to start device.\n");
            return -3;
        }

        printf("Press Enter to stop recording...\n");
        getchar();

        ma_device_uninit(&devicePrimary);
        ma_device_uninit(&deviceSecundary);
        ma_device_uninit(&deviceAux);

        ma_context_uninit(&context);

        char command[512];
        snprintf(command, sizeof(command), "ffmpeg -i %s -ar 16000 %s_16000.wav", file_path, file_path);
        if (system(command) != 0)
        {
            printf("Failed to run ffmpeg command.\n");
            return -4;
        }

        char file_dir[512];
        snprintf(file_dir, sizeof(file_dir), "%s/%s", home, RECORD_FOLDER);

        char whisper_command[1024];
        snprintf(whisper_command, sizeof(whisper_command), "%s/whisper -m %s/ggml-base.bin -l pt %s_16000.wav --output-txt", file_dir, file_dir, file_path);
        printf("Running Whisper command: %s\n", whisper_command);
        if (system(whisper_command) != 0)
        {
            printf("Failed to run Whisper command.\n");
            return -5;
        }

        snprintf(command, sizeof(command), "rm %s_16000.wav", file_path);
        if (system(command) != 0) {
            printf("Error deleting 16000.wav file.\n");
            return -1;
        }        
    }
    else
    {
        printf("Unknown command: %s\n", argv[1]);
    }

    ma_context_uninit(&context);
    return 0;
}
