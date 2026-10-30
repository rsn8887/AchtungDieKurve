Achtung, die Kurve!
=============

A competitive lightcycles game with smooth curves for 2-6 local players, inspired by the Amiga PD classic with the same name.

![Title Screenshot](/Screenshots/Achtung_Die_Kurve_Screenshot_1.png?raw=true "Title Screenshot")

![In-Game Screenshot](/Screenshots/Achtung_Die_Kurve_Screenshot_2.png?raw=true "In-Game Screenshot")

How to build
---------------

- Switch:
```
docker run -it -v "$(pwd)":/app -w /app devkitpro/devkita64
make
```

- MacOS:
```
brew install sdl2
brew install sdl2_ttf
g++ -std=c++11 -O2 achtung.cpp -o achtung `sdl2-config --cflags --libs` -lSDL2_ttf
```

- Windows:
Install MSYS2 MinGW, then open a msys2 ucrt shell
```
pacman -S mingw-w64-ucrt-x86_64-gcc
pacman -S mingw-w64-ucrt-x86_64-SDL2
pacman -S mingw-w64-ucrt-x86_64-SDL2_ttf
curl https://raw.githubusercontent.com/rollerozxa/msys2-bundledlls/master/bundledlls > ./bundledlls
g++ -std=c++11 -O2 achtung.cpp -o achtung `sdl2-config --cflags --libs` -lSDL2_ttf
bundledlls ./achtung.exe ./
```

assisted by Claude
