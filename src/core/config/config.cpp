#include "config.hpp"
#include <chrono>
#include <fancy.hpp>
#include <filesystem>
#include <fstream>
#include <helper/json/bindings.hpp>
#include <string>
#include <Windows.h>

using namespace std;

namespace Soundux::Objects
{
    
    const std::string Config::path = []() -> std::string {
#if defined(__linux__)
        const auto *configPath = std::getenv("XDG_CONFIG_HOME"); // NOLINT
        if (configPath)
        {
            return std::string(configPath) + "/Soundux/config.json";
        }
        return std::string(std::getenv("HOME")) + "/.config/Soundux/config.json"; // NOLINT
#elif defined(_WIN32)

	char buffer[MAX_PATH];
    	GetModuleFileName( NULL, buffer, MAX_PATH );
    	string::size_type pos = std::string( buffer ).find_last_of( "\\/" );

        auto rtn = std::string( buffer ).substr( 0, pos); + "\\Soundux\\config.json";
	free(buffer);
        return rtn;
#endif
    }();

    void Config::save()
    {
        try
        {
            if (!std::filesystem::exists(path))
            {
                std::filesystem::path configFile(path);
                std::filesystem::create_directories(configFile.parent_path());
            }
            std::ofstream configFile(path);
            configFile << nlohmann::json(*this).dump();
            configFile.close();
            Fancy::fancy.logTime().success() << "Config written" << std::endl;
        }
        catch (const std::exception &e)
        {
            Fancy::fancy.logTime().failure() << "Failed to write config: " >> e.what() << std::endl;
        }
        catch (...)
        {
            Fancy::fancy.logTime().failure() << "Failed to write config" << std::endl;
        }
    }
    void Config::load()
    {
        try
        {
            if (!std::filesystem::exists(path))
            {
                Fancy::fancy.logTime().warning() << "Config not found" << std::endl;
                return;
            }

            std::ifstream configStream(path);
            std::string content((std::istreambuf_iterator<char>(configStream)), std::istreambuf_iterator<char>());
            auto json = nlohmann::json::parse(content, nullptr, false);
            if (json.is_discarded())
            {
                Fancy::fancy.logTime().failure() << "Config seems corrupted" << std::endl;
            }
            else
            {
                try
                {
                    auto conf = json.get<Config>();
                    data.set(conf.data);
                    settings = conf.settings;
                    Fancy::fancy.logTime().success() << "Config read" << std::endl;
                }
                catch (...)
                {
                    Fancy::fancy.logTime().warning()
                        << "Found possibly old config format, moving old config..." << std::endl;

                    std::filesystem::path configFile(path);
                    std::filesystem::rename(
                        path,
                        configFile.parent_path() /
                            ("soundux_config_old_" +
                             std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".json"));
                }
            }
            configStream.close();
        }
        catch (const std::exception &e)
        {
            Fancy::fancy.logTime().warning() << "Failed to read config: " << e.what() << std::endl;
        }
        catch (...)
        {
            Fancy::fancy.logTime().warning() << "Failed to read config" << std::endl;
        }
    }
} // namespace Soundux::Objects
