// Patched clip.cpp - make stb_image functions have internal linkage
// by defining them as static before STB_IMAGE_IMPLEMENTATION

#define STBIWDEF static
#define STBIDEF static

#include "clip.cpp"
