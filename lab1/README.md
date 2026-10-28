# Лабораторная работа №2. Камера и освещение

## Сборка и запуск

Нужны Vulkan SDK (с `glslc`), CMake 3.20+, Ninja, компилятор с поддержкой C++20 и git — CMake сам скачивает GLFW, vk-bootstrap, VulkanMemoryAllocator и ImGui.

Из корня проекта:

```bash
cmake --preset release
cmake --build build-release --parallel
./build-release/cg-lab2.exe
```

Для отладочной сборки вместо `release` пишется `debug`. Под Visual Studio 2022 — пресет `msvc-release`, исполняемый файл будет в `build-release/Release/`.

Шейдеры из `shaders/` компилируются в SPIR-V при сборке автоматически.

## Структура

- `source/application.cpp` — сцена, камера, источники света, наборы дескрипторов, конвейеры, анимация, интерфейс, запись команд;
- `source/graphics.cpp`, `graphics.hpp` — буферы через VMA, загрузка шейдеров, создание графического конвейера;
- `source/math.hpp` — векторы и матрицы;
- `source/hexahedron.cpp`, `hexahedron.hpp` — построение гексаэдра с нормалями и раскраска вершин;
- `shaders/scene.vert`, `shaders/scene.frag` — освещение по Блинн-Фонгу;
- `shaders/lines.vert`, `shaders/lines.frag` — рёбра, сетка, оси и траектории без освещения;
- `source/main.cpp`, `source/graphics_internal.*` — стартовый код;
- `CMakeLists.txt`, `CMakePresets.json` — сборка.
