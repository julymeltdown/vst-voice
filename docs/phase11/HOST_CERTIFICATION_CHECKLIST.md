# Host certification checklist

For each host/version/OS/architecture record:

1. clean scan and rescan;
2. instantiate and destroy 50 times;
3. GUI open/close/resize, DPI change and IME lyric edit;
4. state save/reopen and active-load restart behavior;
5. 44.1/48/96 kHz and 32/64/128/257/512 frame buffers;
6. note-on/off/choke, overlapping voices and voice stealing;
7. play/stop/seek/loop and tempo changes;
8. asynchronous edit while transport runs;
9. offline bounce and realtime bounce comparison;
10. crash/hang/leak check and resulting artifact hashes.
