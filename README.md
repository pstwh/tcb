# tcb

`tcb` is a simple tool that allows you to record audio from two devices simultaneously and mix the audio into a single file. This can be especially useful for scenarios like recording both a microphone and an audio output, such as meeting recordings or voiceovers.

## Features

- Record from two devices at once (e.g., microphone and system audio).
- Mix and encode the audio into a single file.
- Playback recorded files (TODO).
- Option to transcribe audio using Whisper (TODO. It just calls compiled whisper for now, in the future it will be integrated into the tool).


## Usage

### Command Overview

```
Usage: tcb <command> [options]

Commands:
    list-devices            List available devices
    list-records            List all recorded files
    play <name/number>      Play a specific record (TODO)
    record <dev1> <dev2>    Record audio from specified devices
           --record-name <name>   Name of the recording
           --language <language>  Language of the recording
```

### Example: Recording Audio

To start recording from two devices, use the `record` command with the device indices:

```bash
$ tcb record 0 1
Recording to file: /home/{user}/tcb/tcb_20241212_010202.wav
Press Enter to stop recording..
```

This will record audio from device `0` (e.g., microphone) and device `1` (e.g., system output) simultaneously, mixing the audio and saving it to a single WAV file.
You can check the device indices by running `tcb list-devices`.


### Transcribing Audio with Whisper

After recording, you can transcribe the audio using Whisper. First, ensure you've installed Whisper and downloaded the models as described above.

1. Start recording:

```bash
$ tcb record 0 1
Recording to file: /home/{user}/tcb/tcb_20241212_010202.wav
Press Enter to stop recording..
```

2. It will automatically use Whisper for transcription.
You will need to have whisper bin and base model inside ```/home/{user}/tcb/``` folder.


3. View the transcription:

```bash
$ cat /home/{user}/tcb/tcb_20241212_010202.wav_16000.txt
```

## TODO

- [x] Implement things in a better way.
- [x] Allow Whisper transcription without external bin calls.
- [ ] Make a better cli interface.
- [ ] Allow streming transcription.
- [ ] Allow more than two devices.
- [ ] Better project structure.
- [ ] Shortcut for model download.
- [ ] Add cpu and gpu build (currently is using cuda by default and you need to build whisper.cpp first).
- [ ] In the future maybe for a new project add a GUI for easier device selection and recording management.