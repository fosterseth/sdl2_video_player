# sdl2 video player

## Overview
A lightweight video player built on the SDL2 and ffmpeg libraries. It can play multiple videos simultaneously, all synced to a master clock.
In addition, the video playback can be controlled via TCP commands, e.g. "open movie.mp4", "play", "pause", "seekto 350.0", etc.
This allows for an easy interface to other applications - Python, Javascript, etc. -- anything that handle basic network sockets.

## Requirements to Compile
Originally built with msys2 and the mingw64 gcc compiler on a Windows 10 machine.

msys2 packages:
- mingw-w64-x86_64-ffmpeg 3.3-1
- mingw-w64-x86_64-SDL2 2.0.5-1

## Cross-platform note
Everything is crossplatform except the socket programming code, read_from_client() method. The current source code has a winsock2 implementation, for Windows. It would be pretty easy to swap this out with linux socket programming.

## Basic Usage
#### vidserv.exe portnum movie1.mp4 movie2.mp4 ...

If you don't specify a port number, it defaults with 50001, which should be open on most machines.

ffmpeg supports a wide range of codecs and formats, so most videos should load fine.

| Hotkey | Command |
| ---    | --- |
| Down | Seek 10 seconds forward |
| Up | Seek 10 seconds backward |
| Right | Show next frame |
| Left | Seek 0.25 seconds backward |
| Space | Toggle play/pause |

## TCP Control

Videos can be opened and controlled via tcp commands.

For example in Python, after setting up tcp connection to vidserv.exe,
```python
sock.send("seekto 60.0".encode())
```

- `open movie1.mp4 xscreen yscreen width height`
    - e.g. open c:/users/fosterseth/desktop/movie.mp4 100 100 640 480
    - To use native width and height of video, just put 0 for both width and height
    
- `seekto x`
    - x is in seconds
 
- `seek+`
    - seeks 60 seconds forward
    
- `seek-`
    - seeks 60 seconds back
    
- `seek+small`
    - seeks to next frame
 
- `seek-small`
    - seeks back 0.25 seconds
    
- `play`

- `pause`

- `toggleplay`
    
- `gettime`
    - a request for the current playback time, in seconds
    - client must call recv() to get the message back

- `getnumvideos`
    - a request for the number of active videos
    - client must call recv() to get message back
    
- `getpos movie1.mp4`
    - a request for the current x,y,w,h of movie1.mp4
    - client must call recv() to get message back
    
- `raisewindow`
    - restores minimized windows
    
- `closewindow`
    - closes all windows, but keeps server running
    
- `break`
    - closes all windows and quits the server
 
 
## Binaries
For convenience, the bin/ folder contains a compiled vidserv and the required DLL to run on a Windows machine.