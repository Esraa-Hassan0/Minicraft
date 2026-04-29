#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "audio.hpp"
#include <iostream>
#include <filesystem>
#include <unordered_map>

namespace our
{
    static ma_engine engine;
    static bool isInitialized = false;
    static std::unordered_map<std::string, ma_sound> loopingSounds;
    static std::unordered_map<std::string, ma_sound> entitySounds;

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

    void AudioSystem::setGlobalVolume(float volume)
    {
        if (isInitialized) {
            ma_engine_set_volume(&engine, volume);
        }
    }

    void AudioSystem::startLoopingSound(const std::string& key, const std::string& filepath)
    {
        if (!isInitialized) return;
        if (loopingSounds.find(key) != loopingSounds.end()) return;

        ma_result result = ma_sound_init_from_file(&engine, filepath.c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC, NULL, NULL, &loopingSounds[key]);
        if (result != MA_SUCCESS) {
            loopingSounds.erase(key);
            return;
        }

        ma_sound_set_looping(&loopingSounds[key], MA_TRUE);
        ma_sound_start(&loopingSounds[key]);
    }

    void AudioSystem::setLoopingSoundVolume(const std::string& key, float volume)
    {
        if (!isInitialized) return;
        auto it = loopingSounds.find(key);
        if (it != loopingSounds.end()) {
            ma_sound_set_volume(&it->second, volume);
        }
    }

    void AudioSystem::stopLoopingSound(const std::string& key)
    {
        if (!isInitialized) return;
        auto it = loopingSounds.find(key);
        if (it != loopingSounds.end()) {
            ma_sound_uninit(&it->second);
            loopingSounds.erase(it);
        }
    }

    void AudioSystem::playEntitySound(const std::string& entityId, const std::string& filepath)
    {
        if (!isInitialized) return;
        
        if (entitySounds.find(entityId) == entitySounds.end()) {
            ma_result result = ma_sound_init_from_file(&engine, filepath.c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC, NULL, NULL, &entitySounds[entityId]);
            if (result != MA_SUCCESS) {
                return;
            }
        }
        
        ma_sound_seek_to_pcm_frame(&entitySounds[entityId], 0);
        ma_sound_start(&entitySounds[entityId]);
    }

    void AudioSystem::stopEntitySound(const std::string& entityId)
    {
        if (!isInitialized) return;
        auto it = entitySounds.find(entityId);
        if (it != entitySounds.end()) {
            ma_sound_stop(&it->second);
        }
    }

    void AudioSystem::destroy()
    {
        if (isInitialized) {
            for (auto& pair : loopingSounds) {
                ma_sound_uninit(&pair.second);
            }
            loopingSounds.clear();
            for (auto& pair : entitySounds) {
                ma_sound_uninit(&pair.second);
            }
            entitySounds.clear();
            ma_engine_uninit(&engine);
            isInitialized = false;
            std::cout << "Miniaudio engine destroyed." << std::endl;
        }
    }
}
