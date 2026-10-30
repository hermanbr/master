# MBS Demo Media

Place the MPEG-TS file used by the public demonstration in this directory. The directory is mounted read-only in the
`test_mbs_af_as` container as `/test/media`.

Use only media that you own or are licensed to exhibit publicly. Convert the source on the host to a low-bitrate
MPEG-TS file:

```bash
ffmpeg -i licensed-demo-video.mp4 \
  -vf 'scale=320:-2,fps=12,format=yuv420p' \
  -c:v libx264 -profile:v baseline -preset veryfast -tune zerolatency \
  -b:v 120k -maxrate 140k -bufsize 280k -g 24 \
  -c:a aac -b:a 24k -ar 32000 -ac 1 \
  -muxrate 180k -f mpegts \
  rt-mbs-examples/test/media/demo.ts
```

Start conservatively at 180 kbit/s. Increase the bitrate only after the gNB and all nrUEs show sustained reception
without RLC reassembly drops.
