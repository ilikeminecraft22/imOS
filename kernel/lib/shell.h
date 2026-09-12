#pragma once
#include "../fs/fat32/fat32.h"
void exec(fat32_t* fs, bool *keep_alive);
void shell_poweron(fat32_t* fs, bool *keep_alive);