# ltjson
Another JSON parser. This one is "Light JSON". The emphasis is on low memory usage and the ability
to free, reuse and/or continue the in memory json tree. This is useful in embedded systems or
messaging systems where the JSON tree needs to be rebuilt over and over without leaking memory
all over the place.

The only oddity is that I've chosen to include C files within other C files. I did this to keep
file sizes reasonably inside a static namespace. There are guard defines on the included files.

I use mingw so there's a quick make.bat to compile test.exe.
