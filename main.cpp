#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include "FileWatch.hpp"
#include "json.hpp"
#include <string>
#include <fstream>
#include <filesystem>
#include <vector>

#define CONFIG_FILE "configs/config.json"

using json = nlohmann::json;

std::atomic<bool> running(true);

void signalHandler(int signum)
{
    std::cout << "\nInterrupt signal (" << signum << ") received. Stopping..." << std::endl;
    running = false;
}

void readFileContents(const std::string &path, int retry_count = 3, int retry_delay_ms = 1000)
{
    for (int attempt = 1; attempt <= retry_count; ++attempt)
    {
        std::cout << "Reading file contents (Attempt " << attempt << ")..." << path << std::endl;
        std::ifstream file(path);
        if (file.is_open())
        {
            std::cout << "\n--- File Contents ---" << std::endl;
            std::string line;
            while (std::getline(file, line))
            {
                std::cout << line << std::endl;
            }
            std::cout << "--- End of File ---\n"
                      << std::endl;
            file.close();
            return; // Success! Exit function
        }

        // Failed to open, check if we should retry
        if (attempt < retry_count)
        {
            std::cerr << "Attempt " << attempt << " failed. Retrying in "
                      << retry_delay_ms << "ms..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
        }
    }

    // All retries exhausted
    std::cerr << "Error: Unable to open file after " << retry_count
              << " attempts: " << path << std::endl;
    std::cerr << "The file may have been deleted or is inaccessible." << std::endl;
}

void runPythonScript(const std::string &script_path)
{
    std::string command = "python3 " + script_path;
    int result = system(command.c_str());
    if (result != 0)
    {
        std::cerr << "Error: Failed to execute script: " << script_path << std::endl;
    }
}

std::string update_command_with_key(const std::string &command, const std::string &key, const std::string &value)
{
    std::string modified_key = "${" + key + "}";
    std::string modified_command = command;
    size_t pos = command.find(modified_key);
    while ((pos = modified_command.find(modified_key, pos)) != std::string::npos)
    {
        modified_command.replace(pos, modified_key.length(), value);
        pos += value.length();
    }
    return modified_command;
}

std::unique_ptr<std::string> prepare_command(const std::string &command, const std::string &path)
{
    std::cout << "Preparing command for path: " << path << std::endl;
    std::string modified_command = update_command_with_key(command, "file", path);
    return std::make_unique<std::string>(modified_command);
    // return std::make_unique<std::string>(command);
}

void performAction(const json &config, const std::string &path)
{
    std::cout << "Performing action for path: " << path << std::endl;
    if (!(config.contains("os_command")))
        return;
    try
    {
        auto command = prepare_command(config["os_command"].get<std::string>(), path);
        std::cout << "Executing command: " << *command << std::endl;
        int result = system(command->c_str());
        if (result != 0)
        {
            std::cerr << "Error: Command execution failed with code " << result << std::endl;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error preparing or executing command: " << e.what() << std::endl;
    }
}

void handleFileChange(const json &config,
                      const std::string &folder,
                      const std::string &path,
                      const filewatch::Event change_type)
{
    std::filesystem::path full_path = std::filesystem::path(folder) / std::filesystem::path(path);
    std::cout << "Detected change in file: " << full_path << std::endl;
    if (full_path.extension() != config["file_extension"].get<std::string>())
    {

        std::cout << "Ignoring file (extension mismatch): " << full_path.extension() << std::endl;
        return;
    }

    // Check if the file still exists for certain events
    if (change_type != filewatch::Event::removed && !std::filesystem::exists(full_path))
    {
        std::cerr << "Warning: File does not exist: " << full_path << std::endl;
        return;
    }
    switch (change_type)
    {
    case filewatch::Event::added:
        if (config["created"].get<bool>())
        {
            performAction(config, full_path.string());
        }
        break;
    case filewatch::Event::removed:
        if (config["deleted"].get<bool>())
        {
            performAction(config, full_path.string());
        }
        break;

    case filewatch::Event::modified:
        std::cout << "File modified: " << path << std::endl;
        if (config["modified"].get<bool>())
        {
            performAction(config, full_path.string());
        }
        break;
    case filewatch::Event::renamed_old:
        if (config["renamed_old"].get<bool>())
        {
            performAction(config, full_path.string());
        }

        break;
    case filewatch::Event::renamed_new:
        if (config["renamed_new"].get<bool>())
        {
            performAction(config, full_path.string());
        }
        break;
    default:
        std::cout << "Unknown event for file: " << path << std::endl;
        break;
    }
}

std::unique_ptr<filewatch::FileWatch<std::string>> registerFileWatch(
    const json &config,
    const std::string &path)
{
    return std::make_unique<filewatch::FileWatch<std::string>>(
        path,
        [config, path](const std::string &file_path, const filewatch::Event change_type)
        {
            handleFileChange(config, path, file_path, change_type);
        });
}

json loadConfig(const std::string &config_path)
{
    std::ifstream config_file(config_path);
    if (!config_file.is_open())
    {
        throw std::runtime_error("Could not open config file: " + config_path);
    }

    json config_json;
    config_file >> config_json;
    return config_json;
}

int main(int argc, char *argv[])
{
    // Register signal handler for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::string path = (argc > 1) ? argv[1] : "./temp_file";
    json config;
    try
    {
        config = loadConfig(CONFIG_FILE);
        std::cout << "Configuration loaded successfully." << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error loading configuration: " << e.what() << std::endl;
        return 1;
        ;
    }
    std::cout << "Loaded Config: " << config.dump(4) << std::endl;

    std::vector<std::unique_ptr<filewatch::FileWatch<std::string>>> watches;

    for (const auto &watch_config : config)
    {
        std::string watch_path = watch_config["folder"].get<std::string>();
        std::cout << "Setting up watch on: " << watch_path << std::endl;

        try
        {
            // Create the watch and move it into the vector
            watches.emplace_back(registerFileWatch(watch_config, watch_path));
            std::cout << "Registered file watch on: " << watch_path << std::endl;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error registering file watch on " << watch_path
                      << ": " << e.what() << std::endl;
        }
    }

    std::cout << "Starting file watch on: " << path << std::endl;
    std::cout << "Press Ctrl+C to stop monitoring..." << std::endl;
    auto path_fs = std::filesystem::path(path);
    if (!std::filesystem::exists(path_fs) || !std::filesystem::is_directory(path_fs))
    {
        std::cerr << "Error: Specified path is not a valid directory: " << path << std::endl;
        return 1;
        ;
    }

    while (running)
    {

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "File monitoring stopped." << std::endl;
    return 0;
}