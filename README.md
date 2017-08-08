# sdl2 video player

Work in progress. A C-based video player made from the ffmpeg library and SDL2 for the audio/video output. It can play multiple videos simultaneously, all synced to a master clock.
In addition, the video playback can be controlled via TCP commands, e.g. "open movie.mp4", "play", "pause", "seekto 350.0", etc.
This allows for an easy interface to other applications - Python, Javascript, etc. -- anything that handle basic network sockets.