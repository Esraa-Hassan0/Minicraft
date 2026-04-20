#pragma once

#include <string>

namespace our
{
    class AudioSystem
    {
    public:
        static void initialize();
        static void playSound(const std::string& filepath);
        static void setGlobalVolume(float volume);
        static void startLoopingSound(const std::string& key, const std::string& filepath);
        static void setLoopingSoundVolume(const std::string& key, float volume);
        static void stopLoopingSound(const std::string& key);
        static void destroy();
    };
}
