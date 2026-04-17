#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "audio.hpp"
#include <iostream>
#include <filesystem>

namespace our
{
    static ma_engine engine;
    static bool isInitialized = false;

    void AudioSystem::initialize()
    {
        if (isInitialized) return;
        
        ma_engine_config config = ma_engine_config_init();
        config.noAutoStart = MA_FALSE;
        
        ma_result result = ma_engine_init(&config, &engine);
        if (result != MA_SUCCESS) {
            return;
        }
        isInitialized = true;
        std::cout << "Miniaudio engine initialized successfully." << std::endl;
    }

    void AudioSystem::playSound(const std::string& filepath)
    {
        ma_engine_play_sound(&engine, filepath.c_str(), NULL);
    }

    void AudioSystem::destroy()
    {
        if (isInitialized) {
            ma_engine_uninit(&engine);
            isInitialized = false;
            std::cout << "Miniaudio engine destroyed." << std::endl;
        }
    }
}
