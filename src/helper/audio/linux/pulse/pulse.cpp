#include "pulse.hpp"
#include <exception>
#include <fancy.hpp>
#include <helper/misc/misc.hpp>
#include <memory>
#include <pulse/introspect.h>
#include <pulse/proplist.h>

namespace Soundux::Objects
{
    void PulseAudio::setup()
    {
        mainloop = pa_mainloop_new();
        mainloopApi = pa_mainloop_get_api(mainloop);
        context = pa_context_new(mainloopApi, "soundux");
        pa_context_connect(context, nullptr, PA_CONTEXT_NOFLAGS, nullptr);

        bool ready = false;
        pa_context_set_state_callback(
            context,
            [](pa_context *context, void *userData) {
                auto state = pa_context_get_state(context);
                if (state == PA_CONTEXT_FAILED)
                {
                    Fancy::fancy.logTime().failure() << "Failed to connect to pulseaudio" << std::endl;
                    std::terminate();
                }
                else if (state == PA_CONTEXT_READY)
                {
                    Fancy::fancy.logTime().message() << "PulseAudio is ready!" << std::endl;
                    *reinterpret_cast<bool *>(userData) = true;
                }
            },
            &ready);

        while (!ready)
        {
            pa_mainloop_iterate(mainloop, true, nullptr);
        }

        unloadLeftOvers();
        fetchDefaultSource();

        auto playbackApps = getPlaybackApps();
        auto recordingApps = getRecordingApps();

        await(pa_context_load_module(
            context, "module-null-sink",
            "sink_name=soundux_sink rate=44100 sink_properties=device.description=soundux_sink",
            []([[maybe_unused]] pa_context *m, std::uint32_t id, void *userData) {
                if (static_cast<int>(id) < 0)
                {
                    Fancy::fancy.logTime().failure() << "Failed to load null sink" << std::endl;
                    std::terminate();
                }
                else
                {
                    *reinterpret_cast<std::uint32_t *>(userData) = id;
                }
            },
            &nullSink));

        await(pa_context_load_module(
            context, "module-loopback",
            ("rate=44100 source=" + defaultSource + " sink=soundux_sink sink_dont_move=true source_dont_move=true")
                .c_str(),
            []([[maybe_unused]] pa_context *m, std::uint32_t id, void *userData) {
                if (static_cast<int>(id) < 0)
                {
                    Fancy::fancy.logTime().failure() << "Failed to load loopback" << std::endl;
                    std::terminate();
                }
                else
                {
                    *reinterpret_cast<std::uint32_t *>(userData) = id;
                }
            },
            &loopBack));

        await(pa_context_load_module(
            context, "module-null-sink",
            "sink_name=soundux_sink_passthrough rate=44100 sink_properties=device.description=soundux_sink_passthrough",
            []([[maybe_unused]] pa_context *m, std::uint32_t id, void *userData) {
                if (static_cast<int>(id) < 0)
                {
                    Fancy::fancy.logTime().failure() << "Failed to load passthrough null sink" << std::endl;
                    std::terminate();
                }
                else
                {
                    *reinterpret_cast<std::uint32_t *>(userData) = id;
                }
            },
            &passthrough));

        await(pa_context_load_module(
            context, "module-loopback",
            "source=soundux_sink_passthrough.monitor sink=soundux_sink source_dont_move=true",
            []([[maybe_unused]] pa_context *m, std::uint32_t id, void *userData) {
                if (static_cast<int>(id) < 0)
                {
                    Fancy::fancy.logTime().failure() << "Failed to load passthrough sink" << std::endl;
                    std::terminate();
                }
                else
                {
                    *reinterpret_cast<std::uint32_t *>(userData) = id;
                }
            },
            &passthroughSink));

        await(pa_context_load_module(
            context, "module-loopback", "source=soundux_sink_passthrough.monitor source_dont_move=true",
            []([[maybe_unused]] pa_context *m, std::uint32_t id, void *userData) {
                if (static_cast<int>(id) < 0)
                {
                    Fancy::fancy.logTime().failure() << "Failed to load passthrough loopback" << std::endl;
                    std::terminate();
                }
                else
                {
                    *reinterpret_cast<std::uint32_t *>(userData) = id;
                }
            },
            &passthroughLoopBack));

        fixPlaybackApps(playbackApps);
        fixRecordingApps(recordingApps);
    }
    void PulseAudio::destroy()
    {
        revertDefault();
        stopSoundInput();
        stopPassthrough();

        //* We only have to unload these 3 because the other modules depend on these and will automatically be deleted
        await(pa_context_unload_module(context, nullSink, nullptr, nullptr));
        await(pa_context_unload_module(context, loopBack, nullptr, nullptr));
        await(pa_context_unload_module(context, passthrough, nullptr, nullptr));
    }
    void PulseAudio::await(pa_operation *operation)
    {
        while (pa_operation_get_state(operation) != PA_OPERATION_DONE)
        {
            pa_mainloop_iterate(mainloop, true, nullptr);
        }
    }
    void PulseAudio::fetchDefaultSource()
    {
        await(pa_context_get_server_info(
            context,
            []([[maybe_unused]] pa_context *context, const pa_server_info *info, void *userData) {
                if (info)
                {
                    reinterpret_cast<PulseAudio *>(userData)->defaultSource = info->default_source_name;
                }
            },
            this));
    }
    void PulseAudio::unloadLeftOvers()
    {
        await(pa_context_get_module_info_list(
            context,
            []([[maybe_unused]] pa_context *ctx, const pa_module_info *info, [[maybe_unused]] int eol, void *userData) {
                if (info && info->argument)
                {
                    if (std::string(info->argument).find("soundux") != std::string::npos)
                    {
                        auto *thiz = reinterpret_cast<PulseAudio *>(userData);
                        pa_context_unload_module(thiz->context, info->index, nullptr, nullptr);
                        Fancy::fancy.logTime().success() << "Unloaded left over module " << info->index << std::endl;
                    }
                }
            },
            this));
    }
    std::vector<std::shared_ptr<PlaybackApp>> PulseAudio::getPlaybackApps()
    {
        std::vector<std::shared_ptr<PlaybackApp>> rtn;
        await(pa_context_get_sink_input_info_list(
            context,
            []([[maybe_unused]] pa_context *ctx, const pa_sink_input_info *info, [[maybe_unused]] int eol,
               void *userData) {
                if (info && info->driver && std::strcmp(info->driver, "protocol-native.c") == 0)
                {
                    PulsePlaybackApp app;

                    app.id = info->index;
                    app.sink = info->sink;
                    app.name = pa_proplist_gets(info->proplist, "application.name");
                    app.pid = std::stoi(pa_proplist_gets(info->proplist, "application.process.id"));
                    app.application = pa_proplist_gets(info->proplist, "application.process.binary");
                    reinterpret_cast<decltype(rtn) *>(userData)->emplace_back(std::make_shared<PulsePlaybackApp>(app));
                }
            },
            &rtn));

        return rtn;
    }
    std::vector<std::shared_ptr<RecordingApp>> PulseAudio::getRecordingApps()
    {
        std::vector<std::shared_ptr<RecordingApp>> rtn;
        await(pa_context_get_source_output_info_list(
            context,
            []([[maybe_unused]] pa_context *ctx, const pa_source_output_info *info, [[maybe_unused]] int eol,
               void *userData) {
                if (info && info->driver && std::strcmp(info->driver, "protocol-native.c") == 0)
                {
                    if (info->resample_method && std::strcmp(info->resample_method, "peaks") == 0)
                    {
                        return;
                    }

                    PulseRecordingApp app;

                    app.id = info->index;
                    app.source = info->source;
                    app.name = pa_proplist_gets(info->proplist, "application.name");
                    app.pid = std::stoi(pa_proplist_gets(info->proplist, "application.process.id"));
                    app.application = pa_proplist_gets(info->proplist, "application.process.binary");
                    reinterpret_cast<decltype(rtn) *>(userData)->emplace_back(std::make_shared<PulseRecordingApp>(app));
                }
            },
            &rtn));

        return rtn;
    }
    bool PulseAudio::useAsDefault()
    {
        Fancy::fancy.logTime().warning() << "(FIXME) useAsDefault not yet implemented" << std::endl;
        // if (!Helpers::run("pactl unload-module " + std::to_string(loopBack)))
        // {
        //     Fancy::fancy.logTime().failure() << "Failed to unload loopback" << std::endl;
        //     return false;
        // }

        // loopBack =
        //     getModuleId("pactl load-module module-loopback rate=44100 source=" + defaultSource + "
        //     sink=soundux_sink");

        // if (!Helpers::run("pactl set-default-source soundux_sink.monitor"))
        // {
        //     Fancy::fancy.logTime().failure() << "Failed to set default source to soundux" << std::endl;
        //     return false;
        // }

        return true;
    }
    bool PulseAudio::revertDefault()
    {
        Fancy::fancy.logTime().warning() << "(FIXME) revertDefault not yet implemented" << std::endl;
        // if (!defaultSource.empty())
        // {
        //     if (!Helpers::run("pactl unload-module " + std::to_string(loopBack)))
        //     {
        //         Fancy::fancy.logTime().failure() << "Failed to unload loopback" << std::endl;
        //         return false;
        //     }

        //     loopBack = getModuleId("pactl load-module module-loopback rate=44100 source=" + defaultSource +
        //                            " sink=soundux_sink sink_dont_move=true");

        //     if (!Helpers::run("pactl set-default-source " + defaultSource))
        //     {
        //         Fancy::fancy.logTime().failure() << "Failed to revert to default source" << std::endl;
        //         return false;
        //     }
        // }
        // else
        // {
        //     Fancy::fancy.logTime().warning() << "Default source is not set" << std::endl;
        // }

        return true;
    }
    bool PulseAudio::passthroughFrom(std::shared_ptr<PlaybackApp> app)
    {
        if (movedPassthroughApplication && movedPassthroughApplication->name == app->name)
        {
            Fancy::fancy.logTime().message()
                << "Ignoring sound passthrough request because requested app is already moved" << std::endl;
            return true;
        }
        if (!stopPassthrough())
        {
            Fancy::fancy.logTime().warning() << "Failed to stop current passthrough" << std::endl;
        }
        if (!app)
        {
            Fancy::fancy.logTime().warning() << "Tried to passthrough to non existant app" << std::endl;
            return false;
        }

        for (const auto &playbackApp : getPlaybackApps())
        {
            auto pulsePlayback = std::dynamic_pointer_cast<PulsePlaybackApp>(playbackApp);

            if (playbackApp->name == app->name)
            {
                bool success = true;

                await(pa_context_move_sink_input_by_name(
                    context, pulsePlayback->id, "soundux_sink_passthrough",
                    []([[maybe_unused]] pa_context *ctx, int success, void *userData) {
                        if (!success)
                        {
                            *reinterpret_cast<bool *>(userData) = false;
                        }
                    },
                    &success));

                if (!success)
                {
                    Fancy::fancy.logTime().warning()
                        << "Failed top move " << pulsePlayback->id << " to passthrough" << std::endl;
                    return false;
                }
            }
        }

        movedPassthroughApplication = std::dynamic_pointer_cast<PulsePlaybackApp>(app);
        return true;
    }

    bool PulseAudio::stopPassthrough()
    {
        if (movedPassthroughApplication)
        {
            bool success = false;
            for (const auto &app : getPlaybackApps())
            {
                auto pulseApp = std::dynamic_pointer_cast<PulsePlaybackApp>(app);

                if (app->name == movedPassthroughApplication->name)
                {
                    await(pa_context_move_sink_input_by_index(
                        context, pulseApp->id, movedPassthroughApplication->sink,
                        []([[maybe_unused]] pa_context *ctx, int success, void *userData) {
                            if (success)
                            {
                                *reinterpret_cast<bool *>(userData) = true;
                            }
                        },
                        &success));
                }
                movedPassthroughApplication.reset();
                return success;
            }
        }

        return true;
    }
    bool PulseAudio::inputSoundTo(std::shared_ptr<RecordingApp> app)
    {
        if (!app)
        {
            Fancy::fancy.logTime().warning() << "Tried to input sound to non existant app" << std::endl;
            return false;
        }
        if (movedApplication && movedApplication->name == app->name)
        {
            Fancy::fancy.logTime().message()
                << "Ignoring sound throughput request because sound is already throughput to requested app"
                << std::endl;
            return true;
        }

        stopSoundInput();

        for (const auto &recordingApp : getRecordingApps())
        {
            auto pulseApp = std::dynamic_pointer_cast<PulseRecordingApp>(recordingApp);

            if (pulseApp->name == app->name)
            {
                bool success = true;
                await(pa_context_move_source_output_by_name(
                    context, pulseApp->id, "soundux_sink.monitor",
                    []([[maybe_unused]] pa_context *ctx, int success, void *userData) {
                        if (!success)
                        {
                            *reinterpret_cast<bool *>(userData) = false;
                        }
                    },
                    &success));

                if (!success)
                {
                    Fancy::fancy.logTime().warning() << "Failed to move " + pulseApp->name << "(" << pulseApp->id
                                                     << ") to soundux sink" << std::endl;
                }
            }
        }

        movedApplication = std::dynamic_pointer_cast<PulseRecordingApp>(app);
        return true;
    }
    bool PulseAudio::stopSoundInput()
    {
        bool success = true;
        if (movedApplication)
        {
            for (const auto &recordingApp : getRecordingApps())
            {
                auto pulseApp = std::dynamic_pointer_cast<PulseRecordingApp>(recordingApp);

                if (pulseApp->name == movedApplication->name)
                {
                    await(pa_context_move_source_output_by_index(
                        context, pulseApp->id, movedApplication->source,
                        []([[maybe_unused]] pa_context *ctx, int success, void *userData) {
                            if (!success)
                            {
                                *reinterpret_cast<bool *>(userData) = false;
                            }
                        },
                        &success));

                    if (!success)
                    {
                        Fancy::fancy.logTime().warning() << "Failed to move " << pulseApp->name << "(" << pulseApp->id
                                                         << ") back to original source" << std::endl;
                        success = false;
                    }
                }
            }
            movedApplication.reset();
        }

        return success;
    }
    std::shared_ptr<PlaybackApp> PulseAudio::getPlaybackApp(const std::string &name)
    {
        for (auto app : getPlaybackApps())
        {
            if (app->name == name)
            {
                return app;
            }
        }

        return nullptr;
    }
    std::shared_ptr<RecordingApp> PulseAudio::getRecordingApp(const std::string &name)
    {
        for (auto app : getRecordingApps())
        {
            if (app->name == name)
            {
                return app;
            }
        }

        return nullptr;
    }

    void PulseAudio::fixPlaybackApps(const std::vector<std::shared_ptr<PlaybackApp>> & /**/)
    {
        Fancy::fancy.logTime().warning() << "(FIXME) fixRecordingApps not yet implemented" << std::endl;
    }
    void PulseAudio::fixRecordingApps(const std::vector<std::shared_ptr<RecordingApp>> & /**/)
    {
        Fancy::fancy.logTime().warning() << "(FIXME) fixRecordingApps not yet implemented" << std::endl;
    }
    bool PulseAudio::muteInput(bool /**/)
    {
        Fancy::fancy.logTime().warning() << "(FIXME) muteInput not yet implemented" << std::endl;
        return false;
    }

    bool PulseAudio::isCurrentlyPassingThrough()
    {
        return movedPassthroughApplication != nullptr;
    }
} // namespace Soundux::Objects