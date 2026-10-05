# Native audio scheduler boundary rerun (2026-10-04)

The offline probe ran with explicit `device=0` and a resolver that always
returns false. No `AudioPlayer::play` call was made and no audio device was
opened.

```text
device=0 playbackCalls=0 sinkCalls=1 queuedPcm=1 sinkQueueLimit=8 accepted=8 rejected=1 vpkWav=1 jumpSeen=2 pausedSeen=2 resumeSeen=2 repeatedSeen=2 reverseSeen=0 recoveredSeen=2 missingPlayed=0 wavMalformed=3
```

The boundary counts remain stable for tick jumps, paused counters, resume,
reverse boundary, and forward recovery. It also rejects empty, truncated, and
out-of-bounds WAV inputs, resolves a known local VPK WAV, and sends its decoded
PCM to an injected in-memory sink after checking PCM format and byte alignment.
Calling `advance()` after `stop()` is cancelled and does not add events; only
`reset()` resumes dispatch. Repeating the same tick leaves counters unchanged,
and resuming from tick 5 dispatches only later events. The missing-resource
path records zero played events. An injected in-memory sink accepts eight
items and rejects the ninth; this validates scheduler behavior on sink queue
rejection without opening the production device. The production sound cache
remains bounded at 32 MiB and 512 entries; entries beyond either limit are
resolved without insertion. The audio target Release build and
`git diff --check` passed.
