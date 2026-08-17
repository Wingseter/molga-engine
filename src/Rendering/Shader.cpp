#include "Rendering/Shader.h"

// Shader intentionally has no runtime compiler, uniform setters, program ID,
// or native backend lifetime. Those responsibilities belong to the offline
// bundle compiler and GraphicsDevice pipeline cache.
