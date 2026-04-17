#pragma once

#include <string>

namespace our
{
    class AudioSystem
    {
    public:
        static void initialize();
        static void playSound(const std::string& filepath);
        static void destroy();
    };
}
