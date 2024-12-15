#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "whisper.h"
#include "tcb.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sndfile.h>
#include <unistd.h>
#include <assert.h>

ma_result tcb_device_init(ma_device_id *deviceId, tcb_device *device)
{
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.pDeviceID = deviceId;
    config.dataCallback = rb_write_callback;

    ma_result result = ma_device_init(NULL, &config, &device->device);
    if (result != MA_SUCCESS)
    {
        fprintf(stderr, "Failed to initialize capture device.\n");
        return result;
    }

    result = ma_pcm_rb_init(device->device.capture.format, device->device.capture.channels, BUFFER_SIZE_IN_FRAMES, NULL, NULL, &device->rb);
    if (result != MA_SUCCESS)
    {
        fprintf(stderr, "Failed to initialize ring buffer.\n");
        return result;
    }

    device->device.pUserData = &device->rb;

    ma_data_converter_config converterConfig = ma_data_converter_config_init(
        device->device.capture.format,
        TARGET_FORMAT,
        device->device.capture.channels,
        TARGET_CHANNELS,
        device->device.sampleRate,
        TARGET_SAMPLE_RATE);

    result = ma_data_converter_init(&converterConfig, NULL, &device->converter);
    if (result != MA_SUCCESS)
    {
        fprintf(stderr, "Failed to initialize data converter.\n");
        return result;
    }

    return MA_SUCCESS;
}

ma_result tcb_device_start(tcb_device *device)
{
    ma_result result = ma_device_start(&device->device);
    if (result != MA_SUCCESS)
    {
        fprintf(stderr, "Failed to start device.\n");
        ma_device_uninit(&device->device);
        return result;
    }

    return MA_SUCCESS;
}

void tcb_device_uninit(tcb_device *device)
{
    ma_device_uninit(&device->device);
    ma_pcm_rb_uninit(&device->rb);
    ma_data_converter_uninit(&device->converter, NULL);
}

ma_result tcb_context_init(tcb_context *context, const char *pFilePath, ma_device_id *primaryDeviceId, ma_device_id *secundaryDeviceId)
{
    ma_result result;
    result = tcb_device_init(primaryDeviceId, &context->primary);
    if (result != MA_SUCCESS)
    {
        fprintf(stderr, "Failed to initialize primary device.\n");
        return result;
    }

    result = tcb_device_init(secundaryDeviceId, &context->secundary);
    if (result != MA_SUCCESS)
    {
        fprintf(stderr, "Failed to initialize secundary device.\n");
        return result;
    }

    ma_encoder_config encoderConfig = ma_encoder_config_init(
        ma_encoding_format_wav,
        TARGET_FORMAT,
        TARGET_CHANNELS,
        TARGET_SAMPLE_RATE);

    result = ma_encoder_init_file(pFilePath, &encoderConfig, &context->encoder);
    if (result != MA_SUCCESS)
    {
        fprintf(stderr, "Failed to initialize output file.\n");
        return result;
    }

    return MA_SUCCESS;
}

void tcb_context_uninit(tcb_context *context)
{
    tcb_device_uninit(&context->primary);
    tcb_device_uninit(&context->secundary);
    ma_encoder_uninit(&context->encoder);
}

void ensure_record_folder()
{
    char *home = getenv("HOME");
    if (home == NULL)
    {
        fprintf(stderr, "Failed to get HOME directory.\n");
        exit(1);
    }

    char folder_path[512];
    snprintf(folder_path, sizeof(folder_path), "%s/%s", home, RECORD_FOLDER);

    struct stat st;
    if (stat(folder_path, &st) == -1)
    {
        if (mkdir(folder_path, 0700) != 0)
        {
            fprintf(stderr, "Failed to create folder: %s\n", folder_path);
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
        fprintf(stderr, "Failed to retrieve device information.\n");
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
    if (home == NULL)
    {
        fprintf(stderr, "Error: HOME environment variable not set.\n");
        return;
    }

    char folder_path[512];
    snprintf(folder_path, sizeof(folder_path), "%s/%s", home, RECORD_FOLDER);

    DIR *dir = opendir(folder_path);
    if (dir == NULL)
    {
        perror("Failed to open record folder");
        fprintf(stderr, "Folder path: %s\n", folder_path);
        return;
    }

    printf("Available Records:\n");
    struct dirent *entry;
    int index = 0;
    while ((entry = readdir(dir)) != NULL)
    {
        char *filename = entry->d_name;
        size_t filenameLen = strlen(filename);

        if (filenameLen > 4 && strcmp(filename + filenameLen - 4, ".wav") == 0)
        {
            filename[filenameLen - 4] = '\0';
            printf("    %d: %s\n", index, filename);

            index++;
        }
    }

    closedir(dir);
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
        fprintf(stderr, "Failed to acquire write buffer.\n");
        return;
    }

    memcpy(rbWrite, pInput, framesToWrite * bytesPerFrame);
    if (ma_pcm_rb_commit_write(rb, framesToWrite) != MA_SUCCESS)
    {
        fprintf(stderr, "Failed to commit write buffer.\n");
    }
    
    (void)pOutput;
}

void *rb_read_thread(void *arg)
{
    tcb_context *tcbContext = (tcb_context *)arg;
    assert(tcbContext != NULL);

    ma_uint32 bytesPerFrameNew = ma_get_bytes_per_frame(TARGET_FORMAT, TARGET_CHANNELS);
    ma_pcm_rb *rb = &tcbContext->primary.rb;
    ma_pcm_rb *rbSecundary = &tcbContext->secundary.rb;
    ma_data_converter *converterPrimary = &tcbContext->primary.converter;
    ma_data_converter *converterSecundary = &tcbContext->secundary.converter;
    ma_encoder *encoder = &tcbContext->encoder;
    assert(encoder != NULL);

    while (1)
    {
        ma_uint32 frameCountPrimary = ma_pcm_rb_available_read(rb);
        ma_uint32 frameCountSecundary = ma_pcm_rb_available_read(rbSecundary);
        ma_uint32 frameCount = ma_min(frameCountPrimary, frameCountSecundary);

        if (frameCount > 0)
        {
            void *rbRead, *rbOtherRead;
            if (ma_pcm_rb_acquire_read(rb, &frameCount, &rbRead) == MA_SUCCESS &&
                ma_pcm_rb_acquire_read(rbSecundary, &frameCount, &rbOtherRead) == MA_SUCCESS)
            {

                ma_int16 *rbBuffer = (ma_int16 *)rbRead;
                ma_int16 *rbOtherBuffer = (ma_int16 *)rbOtherRead;

                ma_uint64 frameCountOld = frameCount;
                ma_uint64 frameCountConvertedPrimary, frameCountConvertedSecundary;

                if (ma_data_converter_get_expected_output_frame_count(converterPrimary, frameCountOld, &frameCountConvertedPrimary) != MA_SUCCESS ||
                    ma_data_converter_get_expected_output_frame_count(converterSecundary, frameCountOld, &frameCountConvertedSecundary) != MA_SUCCESS)
                {
                    fprintf(stderr, "Failed to get expected output frame count.\n");
                    ma_pcm_rb_commit_read(rb, frameCount);
                    ma_pcm_rb_commit_read(rbSecundary, frameCount);
                    continue;
                }

                ma_uint64 frameCountConverted = ma_min(frameCountConvertedPrimary, frameCountConvertedSecundary);
                ma_float *rbBufferConverted = malloc(frameCountConverted * bytesPerFrameNew);
                ma_float *rbOtherBufferConverted = malloc(frameCountConverted * bytesPerFrameNew);
                if (!rbBufferConverted || !rbOtherBufferConverted)
                {
                    fprintf(stderr, "Failed to allocate memory for converted buffers\n");
                    ma_pcm_rb_commit_read(rb, frameCount);
                    ma_pcm_rb_commit_read(rbSecundary, frameCount);
                    continue;
                }

                ma_uint64 framesConvertedPrimary = frameCountConverted;
                ma_uint64 framesConvertedSecundary = frameCountConverted;

                if (ma_data_converter_process_pcm_frames(converterPrimary, rbBuffer, &frameCountOld, rbBufferConverted, &framesConvertedPrimary) != MA_SUCCESS ||
                    (frameCountOld = frameCount,
                     ma_data_converter_process_pcm_frames(converterSecundary, rbOtherBuffer, &frameCountOld, rbOtherBufferConverted, &framesConvertedSecundary) != MA_SUCCESS))
                {
                    fprintf(stderr, "Failed to convert buffer(s).\n");
                    free(rbBufferConverted);
                    free(rbOtherBufferConverted);
                    ma_pcm_rb_commit_read(rb, frameCount);
                    ma_pcm_rb_commit_read(rbSecundary, frameCount);
                    continue;
                }

                for (ma_uint32 i = 0; i < frameCountConverted; i++)
                {
                    rbBufferConverted[i] = ma_clamp((rbBufferConverted[i] + rbOtherBufferConverted[i]), -1.0f, 1.0f);
                }

                ma_uint64 framesWritten;
                if (ma_encoder_write_pcm_frames(encoder, rbBufferConverted, frameCountConverted, &framesWritten) != MA_SUCCESS)
                {
                    fprintf(stderr, "Failed to write to encoder.\n");
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

static void cb_log_disable(enum ggml_log_level, const char *, void *) {}

int main(int argc, char **argv)
{
    ensure_record_folder();

    ma_context context;
    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS)
    {
        fprintf(stderr, "Failed to initialize context.\n");
        return -1;
    }

    if (argc < 2)
    {
        printf("Usage: %s <command> [options]\n", argv[0]);
        printf("Commands:\n");
        printf("    list-devices            List available devices\n");
        printf("    list-records            List all recorded files\n");
        printf("    record <dev1> <dev2>   Record using specified devices\n");
        printf("           --record-name <name>   Name of the recording\n");
        printf("           --language <language>  Language of the recording\n");
        printf("           --use-gpu       Use gpu inference \n");
        printf("           --no-transcribe   Do not transcribe after recording\n");
        printf("    transcribe <file>       Transcribe a specific file\n");
        printf("           --language <language>  Language of the recording\n");
        printf("           --use-gpu       Use gpu inference \n");
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
    else if (strcmp(argv[1], "transcribe") == 0)
    {
        if (argc < 3)
        {
            fprintf(stderr, "Specify the file name to transcribe.\n");
            return -1;
        }

        char *language = "pt";
        bool use_gpu = false;
        for (int i = 0; i < argc; i++)
        {
            if (strcmp(argv[i], "--language") == 0 && i + 1 < argc)
            {
                language = argv[i + 1];
                continue;
            }

            if (strcmp(argv[i], "--use-gpu") == 0)
            {
                use_gpu = true;
                continue;
            }
        }

        char *filePath = argv[2];
        const char *dot = strrchr(filePath, '.');
        if (!dot || dot == filePath)
        {
            fprintf(stderr, "Error: input file does not have a valid extension\n");
            return -1;
        }

        printf("Transcribing file: %s\n", filePath);
        ma_decoder decoder;
        if (ma_decoder_init_file(filePath, NULL, &decoder) != MA_SUCCESS)
        {
            fprintf(stderr, "Failed to initialize decoder for file: %s\n", filePath);
            return -1;
        }

        ma_uint64 framesSize;
        if (ma_decoder_get_length_in_pcm_frames(&decoder, &framesSize) != MA_SUCCESS)
        {
            fprintf(stderr, "Failed to get length of file.\n");
        }

        ma_uint32 bytesPerFrame = ma_get_bytes_per_frame(decoder.outputFormat, decoder.outputChannels);
        ma_uint32 bytesPerFrameNew = ma_get_bytes_per_frame(TARGET_FORMAT, TARGET_CHANNELS);
        ma_uint64 bufferSize = framesSize * bytesPerFrame;

        ma_float *audioBuffer = malloc(bufferSize);
        ma_uint64 framesRead;
        if (ma_decoder_read_pcm_frames(&decoder, audioBuffer, framesSize, &framesRead) != MA_SUCCESS)
        {
            fprintf(stderr, "Failed to read audio data.\n");
        }

        ma_data_converter_config converterConfig = ma_data_converter_config_init(
            decoder.outputFormat,
            TARGET_FORMAT,
            decoder.outputChannels,
            TARGET_CHANNELS,
            decoder.outputSampleRate,
            TARGET_SAMPLE_RATE);

        ma_data_converter converter;
        ma_result result = ma_data_converter_init(&converterConfig, NULL, &converter);
        if (result != MA_SUCCESS)
        {
            fprintf(stderr, "Failed to initialize data converter.\n");
            return result;
        }

        ma_uint64 framesReadOut;
        if (ma_data_converter_get_expected_output_frame_count(&converter, framesRead, &framesReadOut) != MA_SUCCESS)
        {
            fprintf(stderr, "Failed to get expected output frame count.\n");
        }

        ma_float *audioBufferConverted = malloc(framesReadOut * bytesPerFrameNew);

        ma_uint64 frameCountOut;
        result = ma_data_converter_process_pcm_frames(&converter, audioBuffer, &framesSize, audioBufferConverted, &frameCountOut);
        if (result != MA_SUCCESS)
        {
            fprintf(stderr, "Failed to convert buffer(s).\n");
        }

        char *home = getenv("HOME");
        char model_path[512];
        snprintf(model_path, sizeof(model_path), "%s/%s/%s", home, RECORD_FOLDER, MODEL_FILE);
        whisper_log_set(cb_log_disable, NULL);

        struct whisper_context_params cparams = whisper_context_default_params();
        cparams.use_gpu = use_gpu;
        cparams.flash_attn = true;
        cparams.dtw_aheads_preset = WHISPER_AHEADS_LARGE_V3_TURBO;
        struct whisper_context *ctx = whisper_init_from_file_with_params(model_path, cparams);
        if (!ctx)
        {
            fprintf(stderr, "Failed to initialize whisper context.\n");
            return 1;
        }

        struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        wparams.language = language;
        wparams.n_threads = 4;
        wparams.strategy = WHISPER_SAMPLING_BEAM_SEARCH;
        wparams.beam_search.beam_size = 5;

        if (whisper_full_parallel(ctx, wparams, audioBufferConverted, framesReadOut, 1) != 0)
        {
            fprintf(stderr, "Failed to process audio\n");
            free(audioBufferConverted);
            whisper_free(ctx);
            return 1;
        }

        char *output_filepath = (char *)malloc(strlen(filePath) + 1);
        strcpy(output_filepath, filePath);
        char *extension = strstr(output_filepath, dot);
        strcpy(extension, ".txt");
        FILE *outfile = fopen(output_filepath, "w");
        printf("Transcription saved to: %s\n", output_filepath);

        const int segments = whisper_full_n_segments(ctx);
        for (int i = 0; i < segments; i++)
        {
            const char *text = whisper_full_get_segment_text(ctx, i);
            printf("%s\n", text);
            fprintf(outfile, "%s\n", text);
        }
        fclose(outfile);
        free(audioBuffer);
        whisper_free(ctx);
    }
    else if (strcmp(argv[1], "record") == 0)
    {
        if (argc < 4)
        {
            fprintf(stderr, "Specify primary and secondary device IDs for recording.\n");
            return -1;
        }

        int primaryDeviceId = atoi(argv[2]);
        int secondaryDeviceId = atoi(argv[3]);

        char filePath[512];

        char *filePrefix = "tcb";
        char *language = "pt";
        bool use_gpu = false;
        bool no_transcribe = false;
        for (int i = 0; i < argc; i++)
        {
            if (strcmp(argv[i], "--record-name") == 0 && i + 1 < argc)
            {
                filePrefix = argv[i + 1];
                continue;
            }

            if (strcmp(argv[i], "--language") == 0 && i + 1 < argc)
            {
                language = argv[i + 1];
                continue;
            }

            if (strcmp(argv[i], "--use-gpu") == 0)
            {
                use_gpu = true;
                continue;
            }

            if (strcmp(argv[i], "--no-transcribe") == 0)
            {
                no_transcribe = true;
                continue;
            }
        }

        char *home = getenv("HOME");

        time_t now = time(NULL);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));

        snprintf(filePath, sizeof(filePath), "%s/%s/%s_%s.wav", home, RECORD_FOLDER, filePrefix, timestamp);

        printf("Recording to file: %s\n", filePath);

        ma_context context;
        if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS)
        {
            fprintf(stderr, "Failed to initialize context.\n");
            return -2;
        }

        ma_device_info *pPlaybackDeviceInfos;
        ma_uint32 playbackDeviceCount;
        ma_device_info *pCaptureDeviceInfos;
        ma_uint32 captureDeviceCount;
        if (ma_context_get_devices(&context, &pPlaybackDeviceInfos, &playbackDeviceCount, &pCaptureDeviceInfos, &captureDeviceCount) != MA_SUCCESS)
        {
            fprintf(stderr, "Failed to retrieve device information.\n");
            return -3;
        }

        tcb_context tcbContext;
        ma_result result = tcb_context_init(&tcbContext, filePath, &pCaptureDeviceInfos[primaryDeviceId].id, &pCaptureDeviceInfos[secondaryDeviceId].id);
        if (result != MA_SUCCESS)
        {
            fprintf(stderr, "Failed to initialize tcb context.\n");
        }

        if (tcb_device_start(&tcbContext.primary) != MA_SUCCESS)
        {
            fprintf(stderr, "Failed to start primary device.\n");
        }

        if (tcb_device_start(&tcbContext.secundary) != MA_SUCCESS)
        {
            fprintf(stderr, "Failed to start secundary device.\n");
        }

        pthread_t thread;
        pthread_create(&thread, NULL, rb_read_thread, (void *)(&tcbContext));

        printf("Press Enter to stop recording...\n");
        getchar();

        tcb_context_uninit(&tcbContext);
        pthread_cancel(thread);

        ma_context_uninit(&context);

        printf("Audio file saved to: %s\n", filePath);

        if (!no_transcribe)
        {
            ma_decoder_config decoder_config = ma_decoder_config_init(TARGET_FORMAT, TARGET_CHANNELS, TARGET_SAMPLE_RATE);

            ma_decoder decoder;
            if (ma_decoder_init_file(filePath, &decoder_config, &decoder) != MA_SUCCESS)
            {
                fprintf(stderr, "Failed to initialize decoder for file: %s\n", filePath);
            }

            ma_uint64 framesSize;
            if (ma_decoder_get_length_in_pcm_frames(&decoder, &framesSize) != MA_SUCCESS)
            {
                fprintf(stderr, "Failed to get length of file.\n");
            }

            ma_uint32 bytesPerFrameNew = ma_get_bytes_per_frame(TARGET_FORMAT, TARGET_CHANNELS);
            ma_uint64 bufferSize = framesSize * bytesPerFrameNew;

            ma_float *audioBuffer = malloc(bufferSize);
            ma_uint64 framesRead;
            if (ma_decoder_read_pcm_frames(&decoder, audioBuffer, framesSize, &framesRead) != MA_SUCCESS)
            {
                fprintf(stderr, "Failed to read audio data.\n");
            }

            char model_path[512];
            snprintf(model_path, sizeof(model_path), "%s/%s/%s", home, RECORD_FOLDER, MODEL_FILE);
            whisper_log_set(cb_log_disable, NULL);
            struct whisper_context_params cparams = whisper_context_default_params();
            cparams.use_gpu = use_gpu;
            cparams.flash_attn = true;
            cparams.dtw_aheads_preset = WHISPER_AHEADS_LARGE_V3_TURBO;
            struct whisper_context *ctx = whisper_init_from_file_with_params(model_path, cparams);
            if (!ctx)
            {
                fprintf(stderr, "Failed to initialize whisper context.\n");
                return 1;
            }

            struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
            wparams.language = language;
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

            char *output_filepath = (char *)malloc(strlen(filePath) + 1);
            strcpy(output_filepath, filePath);
            char *extension = strstr(output_filepath, ".wav");
            if (extension == NULL)
            {
                fprintf(stderr, "Error: input file is not a .wav file\n");
                free(audioBuffer);
                whisper_free(ctx);
                return 1;
            }

            strcpy(extension, ".txt");

            FILE *outfile = fopen(output_filepath, "w");
            printf("Transcription saved to: %s\n", output_filepath);

            const int segments = whisper_full_n_segments(ctx);
            for (int i = 0; i < segments; i++)
            {
                const char *text = whisper_full_get_segment_text(ctx, i);
                printf("%s\n", text);
                fprintf(outfile, "%s\n", text);
            }
            fclose(outfile);
            free(audioBuffer);
            whisper_free(ctx);
        }
    }
    else
    {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
    }

    ma_context_uninit(&context);
    return 0;
}