#ifndef TCB_H
#define TCB_H

#include "miniaudio/miniaudio.h"
#include "whisper.h"
#include <pthread.h>

#define RECORD_FOLDER ".tcb"
#define BUFFER_SIZE_IN_FRAMES 1024 * 16

#define TARGET_FORMAT ma_format_f32
#define TARGET_CHANNELS 1
#define TARGET_SAMPLE_RATE 16000

#define MODEL_FILE "ggml-large-v3-turbo-q5_0.bin"

typedef struct tcb_device tcb_device;
typedef struct tcb_context tcb_context;

void rb_write_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount);
ma_result tcb_device_init(ma_device_id *device_id, tcb_device *device);
ma_result tcb_device_start(tcb_device *device);
void tcb_device_uninit(tcb_device *device);
ma_result tcb_context_init(tcb_context *context, const char *pFilePath, ma_device_id *primary_device_id, ma_device_id *secundary_device_id);
void tcb_context_uninit(tcb_context *context);
void ensure_record_folder();
void list_devices(ma_context *context);
void list_records();
void *rb_read_thread(void *arg);

struct tcb_device
{
    ma_device device;
    ma_pcm_rb rb;
    ma_data_converter converter;
};

struct tcb_context
{
    tcb_device primary;
    tcb_device secundary;
    ma_encoder encoder;
};

#endif