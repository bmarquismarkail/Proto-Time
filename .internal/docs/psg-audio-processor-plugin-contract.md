# PSG Audio Processor Plugin Contract

Proto-Time publishes source-rate audio blocks from PSG/APU cores to a bounded
single-producer/single-consumer queue. The emulation lane owns guest sound state
and packet construction. The audio worker owns processor execution, resampling,
and ready-block production. Device callbacks only drain ready blocks.

## Source blocks

`RealtimeAudioPacket`/`AudioSourceBlock` contract version 2 carries:

- interleaved signed 16-bit dry PCM;
- four voice descriptors;
- voice-major stems, with one interleaved stream per voice;
- ordered PSG events whose offsets are measured in source sample frames;
- frame, absolute sample, and lifecycle epoch metadata.

Game Gear voices are tone 1, tone 2, tone 3, and noise. Game Boy voices are
pulse 1, pulse 2, wave, and noise. Canonical events describe snapshots, gates,
retriggers, pitch, level, routing, and timbre. A paired raw-write event retains
the original port/register and value when applicable.

## Pure-C processor ABI

Modules advertise `TIME_PLUGIN_KIND_AUDIO_PROCESSOR_V1` and a
`TimeAudioProcessorApiV1`. `process` receives borrowed pointers valid only for
the call and a caller-owned output buffer. Returning `PROCESSED` requires exactly
the same sample count and format as the input. `BYPASS` preserves the current
mix. `ERROR` preserves the current block and disables that adapter.

Processors run in command-line order before stateful sample-rate conversion.
Each processor receives the preceding processor's mix and the original immutable
voice stems/events. Event-only MIDI adapters should return `BYPASS` and move OS
MIDI I/O onto their own bounded thread. Processor callbacks must not block.

Load modules with repeatable `--audio-processor-plugin <path>`. Supply optional
JSON configuration with `--audio-processor-config <plugin-id>=<json-file>`.
All audio-processor descriptors in a module load in descriptor order.

## Lifecycle and failure behavior

Reset, ROM load, save-state restore, and backend restart advance the audio epoch,
drop stale source/ready blocks, and call processor `flush`. Core reset packets
begin with a state snapshot for every voice. Plugins should treat `flush` as an
all-notes-off boundary.

The source queue never blocks machine stepping. A full queue drops the new block
and increments `sourceQueueOverruns`; the ready FIFO's existing underrun and
zero-fill behavior remains authoritative at the device boundary.
