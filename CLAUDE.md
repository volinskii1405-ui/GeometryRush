# Правила для Claude в этом репозитории

## После каждого изменения игры или симулятора

Пользователь работает на **Linux Fedora**. В конце каждой задачи, которая меняет код:

1. **Всегда давай команды запуска из исходников под Fedora** (зависимости через `dnf`, клон/распаковка,
   сборка CMake, запуск `./build/GeometryRush` и `./build/FlightSim`). Шаблон:

   ```bash
   sudo dnf install -y gcc-c++ cmake git make \
       libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel \
       mesa-libGL-devel alsa-lib-devel
   git clone -b <ветка> https://github.com/volinskii1405-ui/GeometryRush.git
   cd GeometryRush
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j"$(nproc)"
   ./build/FlightSim      # авиасимулятор
   ./build/GeometryRush   # 2D-игра
   ```

2. **Всегда присылай сами архивы** через SendUserFile:
   * `GeometryRush-src.tar.gz` — исходники (`git archive --prefix=GeometryRush/ HEAD`);
   * `GeometryRush-linux-x86_64.tar.gz` — готовые бинарники `FlightSim`, `GeometryRush` и папка `levels/`.
     Собирать в отдельной папке `build-dist` с
     `-DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"`, чтобы бинарники не зависели
     от версии libstdc++ на Fedora. Проверить `objdump -T ... | grep GLIBC_` — указать минимальную glibc.

   Архивы класть в каталог scratchpad сессии (не в репозиторий).
