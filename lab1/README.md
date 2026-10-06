# Лабораторная работа №1. Основы 3D-графики

## Сборка и запуск

Нужны Vulkan SDK (с `glslc`), CMake 3.20+, Ninja, компилятор с поддержкой C++20 и git — CMake сам скачивает GLFW, vk-bootstrap, VulkanMemoryAllocator и ImGui.

Из корня проекта:

```bash
cmake --preset release
cmake --build build-release --parallel
./build-release/cg-lab1.exe
```

Для отладочной сборки вместо `release` пишется `debug`. Под Visual Studio 2022 — пресет `msvc-release`, исполняемый файл будет в `build-release/Release/`.

Шейдеры из `shaders/` компилируются в SPIR-V при сборке автоматически.

## Структура

- `source/application.cpp` — сцена, наборы дескрипторов, конвейеры, анимация, интерфейс, запись команд;
- `source/graphics.cpp`, `graphics.hpp` — буферы через VMA, загрузка шейдеров, создание графического конвейера;
- `source/math.hpp` — векторы и матрицы;
- `source/hexahedron.cpp`, `hexahedron.hpp` — построение и раскраска гексаэдра;
- `shaders/scene.vert`, `shaders/scene.frag` — шейдеры;
- `source/main.cpp`, `source/graphics_internal.*` — стартовый код;
- `CMakeLists.txt`, `CMakePresets.json` — сборка.
