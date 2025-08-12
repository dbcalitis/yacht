# yacht

Ride the WAVs.

TODO
- [ ] ui
    - [X] display duration
        - [X] elapsed time / total duration
    - [X] playback status (e.g. playing/paused/stopped)
    - [ ] scrollable display
         - [ ] highlight selected track
- [ ] feedback/errors
    - [ ] feedback for successful actions (e.g., track added to playlist)
    - [ ] clear error msgs (e.g., file not found)
- [ ] playback controls
    - [X] seek (forward/backward)
    - [X] pause/cancel
    - [X] audio loop
    - [ ] playlist loop
    - [ ] next/previous track
    - [ ] mute
- [ ] equalizer
    - [X] biquad filters
    - [ ] allow customized params
        - [ ] cmdline
            - [X] txt file
            - [ ] flags
        - [ ] ui (through keys)
- [ ] file navigation
    - [X] file search
    - [ ] add audio files from different locations
    - [ ] add folder containing audio files
- [ ] playlists
    - [ ] create/delete playlists
        - [ ] name playlists
    - [ ] add/remove songs
- [ ] settings
    - [ ] customizable keybinds
- [X] format support
    - [X] WAV files
        - [X] 16-bit
        - [X] 24-bit
        - [X] 32-bit
