#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

class FTSScenarioInputCancelled final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class FTSScenarioConsole final
{
public:
    FTSScenarioConsole(std::istream& Input, std::ostream& Output) noexcept;

    void Write(std::string_view Text);
    void WriteLine(std::string_view Text = {});

    [[nodiscard]]
    std::string ReadString(
        std::string_view Prompt,
        std::optional<std::string_view> DefaultValue = std::nullopt
    );

    [[nodiscard]]
    std::string ReadNonEmptyString(
        std::string_view Prompt,
        std::optional<std::string_view> DefaultValue = std::nullopt
    );

    [[nodiscard]]
    bool ReadYesNo(
        std::string_view Prompt,
        std::optional<bool> DefaultValue = std::nullopt
    );

    [[nodiscard]]
    std::int64_t ReadInteger(
        std::string_view Prompt,
        std::int64_t Minimum,
        std::int64_t Maximum,
        std::optional<std::int64_t> DefaultValue = std::nullopt
    );

    [[nodiscard]]
    std::chrono::milliseconds ReadMilliseconds(
        std::string_view Prompt,
        std::chrono::milliseconds Minimum,
        std::optional<std::chrono::milliseconds> DefaultValue = std::nullopt
    );

    [[nodiscard]]
    std::size_t ReadMenuSelection(
        std::string_view Prompt,
        std::size_t Maximum
    );

private:
    [[nodiscard]]
    std::string ReadRawLine(std::string_view Prompt);

    std::istream& Input;
    std::ostream& Output;
};
