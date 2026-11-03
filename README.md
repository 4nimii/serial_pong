# Serial Pong

Pong but controlled by any serial device capable of sending a stream of values in the format `value\n`.

For instance:
```
1.2\n56.2\n89\n
```
It would read more naturally as:
```
1.2
56.2
89
```
But you probably got it :).

## Build

```bash
# You need SDL2 header files
sudo dnf install sdl2-devel
sudo apt install sdl2-dev
# Everything else should probably already be included by any Linux system
# Or we installing dev. tools like gcc.

g++ pong.cpp -o pong `pkg-config --cflags --libs sdl2`
```

## Start

```bash
./pong
# Or whatever you renamed the executable as.
```
