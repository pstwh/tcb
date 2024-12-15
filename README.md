# tcb

`tcb` is a simple tool that allows you to record audio from two devices simultaneously and mix the audio into a single file. This can be especially useful for scenarios like recording both a microphone and an audio output, such as meeting recordings or voiceovers.

## Features

- Record from two devices at once (e.g., microphone and system audio).
- Mix and encode the audio into a single file.
- Transcribe audio using Whisper.


## Usage

### Command Overview

```
Usage: tcb <command> [options]

Commands:
    list-devices            List available devices
    list-records            List all recorded files
    record <dev1> <dev2>    Record audio from specified devices
           --record-name <name>   Name of the recording
           --language <language>  Language of the recording
           --use-gpu       Use gpu inference
           --no-transcribe   Do not transcribe after recording
    transcribe <file>       Transcribe a specific file
           --language <language>  Language of the recording
           --use-gpu       Use gpu inference
```

### Example: Listing Devices

To list the available devices, run `tcb list-devices`.

```bash
$ tcb list-devices
Playback Devices:
    0: Headphone
Capture Devices:
    0: Microphone
    1: Monitor Headphone
```

### Example: Listing Records

To list all recorded files, run `tcb list-records`.

```bash
$ tcb list-records
Available Records:
    0: tcb_20241212_010202.wav
    1: tcb_20241212_010203.wav
```

### Example: Recording Audio and Transcribing

To start recording from two devices, use the `record` command with the device indices:

```bash
$ tcb record 0 1 --record-name "My Recording" --language "en" --use-gpu
Recording to file: /home/{user}/tcb/tcb_20241212_010202.wav
Press Enter to stop recording..
```

This will record audio from device `0` (e.g., microphone) and device `1` (e.g., system output) simultaneously, mixing the audio and saving it to a single WAV file.
You can check the device indices by running `tcb list-devices`.


### Transcribing Existing Audio with Whisper

After recording, you can transcribe the audio using Whisper. First, ensure you've installed Whisper and downloaded the models as described above.

1. Start recording:

```bash
$ tcb transcribe /home/{user}/tcb/tcb_20241212_010202.wav --language "en" --use-gpu
```


## TODO

- [x] Implement things in a better way.
- [x] Allow Whisper transcription without external bin calls.
- [ ] Make a better cli interface.
- [ ] Allow streming transcription.
- [ ] Allow more than two devices.
- [ ] Better project structure.
- [ ] Shortcut for model download.
- [~] Add cpu and gpu build (currently is using cuda by default and you need to build whisper.cpp first).
- [ ] In the future maybe for a new project add a GUI for easier device selection and recording management.