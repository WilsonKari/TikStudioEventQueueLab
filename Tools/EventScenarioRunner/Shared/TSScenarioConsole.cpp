#include "Shared/TSScenarioConsole.h"

#include <charconv>
#include <cctype>
#include <istream>
#include <limits>
#include <ostream>
#include <string>
#include <system_error>

namespace
{
    [[nodiscard]]
    std::string_view Trim(std::string_view Text) noexcept
    {
        while (!Text.empty() &&
            std::isspace(static_cast<unsigned char>(Text.front())) != 0)
        {
            Text.remove_prefix(1);
        }

        while (!Text.empty() &&
            std::isspace(static_cast<unsigned char>(Text.back())) != 0)
        {
            Text.remove_suffix(1);
        }

        return Text;
    }

    [[nodiscard]]
    std::string ToLowerAscii(std::string_view Text)
    {
        std::string Result;
        Result.reserve(Text.size());
        for (const unsigned char Character : Text)
        {
            Result.push_back(static_cast<char>(std::tolower(Character)));
        }
        return Result;
    }
}

FTSScenarioConsole::FTSScenarioConsole(
    std::istream& InInput,
    std::ostream& InOutput
) noexcept
    : Input(InInput)
    , Output(InOutput)
{
}

void FTSScenarioConsole::Write(std::string_view Text)
{
    Output << Text;
}

void FTSScenarioConsole::WriteLine(std::string_view Text)
{
    Output << Text << '\n';
}

std::string FTSScenarioConsole::ReadRawLine(std::string_view Prompt)
{
    Output << Prompt;
    Output.flush();

    std::string Line;
    if (!std::getline(Input, Line))
    {
        throw FTSScenarioInputCancelled(
            "Entrada finalizada; la configuración fue cancelada."
        );
    }

    return Line;
}

std::string FTSScenarioConsole::ReadString(
    std::string_view Prompt,
    std::optional<std::string_view> DefaultValue
)
{
    std::string Line = ReadRawLine(Prompt);
    if (Line.empty() && DefaultValue.has_value())
    {
        return std::string(*DefaultValue);
    }

    return Line;
}

std::string FTSScenarioConsole::ReadNonEmptyString(
    std::string_view Prompt,
    std::optional<std::string_view> DefaultValue
)
{
    while (true)
    {
        std::string Value = ReadString(Prompt, DefaultValue);
        if (!Trim(Value).empty())
        {
            return Value;
        }

        WriteLine("El valor no puede estar vacío.");
    }
}

bool FTSScenarioConsole::ReadYesNo(
    std::string_view Prompt,
    std::optional<bool> DefaultValue
)
{
    while (true)
    {
        const std::string Line = ReadRawLine(Prompt);
        const std::string_view Trimmed = Trim(Line);
        if (Trimmed.empty() && DefaultValue.has_value())
        {
            return *DefaultValue;
        }

        const std::string Normalized = ToLowerAscii(Trimmed);
        if (Normalized == "s" || Normalized == "si" ||
            Normalized == "sí" || Normalized == "y" ||
            Normalized == "yes")
        {
            return true;
        }

        if (Normalized == "n" || Normalized == "no")
        {
            return false;
        }

        WriteLine("Escribe sí o no.");
    }
}

std::int64_t FTSScenarioConsole::ReadInteger(
    std::string_view Prompt,
    std::int64_t Minimum,
    std::int64_t Maximum,
    std::optional<std::int64_t> DefaultValue
)
{
    if (Minimum > Maximum)
    {
        throw std::invalid_argument("Invalid console integer range");
    }

    if (DefaultValue.has_value() &&
        (*DefaultValue < Minimum || *DefaultValue > Maximum))
    {
        throw std::invalid_argument("Console integer default is out of range");
    }

    while (true)
    {
        const std::string Line = ReadRawLine(Prompt);
        const std::string_view Trimmed = Trim(Line);
        if (Trimmed.empty() && DefaultValue.has_value())
        {
            return *DefaultValue;
        }

        std::int64_t Value = 0;
        const char* const Begin = Trimmed.data();
        const char* const End = Trimmed.data() + Trimmed.size();
        const std::from_chars_result ParseResult =
            std::from_chars(Begin, End, Value);
        if (ParseResult.ec == std::errc{} &&
            ParseResult.ptr == End &&
            Value >= Minimum &&
            Value <= Maximum)
        {
            return Value;
        }

        WriteLine("Introduce un entero dentro del rango permitido.");
    }
}

std::chrono::milliseconds FTSScenarioConsole::ReadMilliseconds(
    std::string_view Prompt,
    std::chrono::milliseconds Minimum,
    std::optional<std::chrono::milliseconds> DefaultValue
)
{
    const std::int64_t Value = ReadInteger(
        Prompt,
        Minimum.count(),
        std::numeric_limits<std::int64_t>::max(),
        DefaultValue.has_value()
            ? std::optional<std::int64_t>{DefaultValue->count()}
            : std::nullopt
    );
    return std::chrono::milliseconds{Value};
}

std::size_t FTSScenarioConsole::ReadMenuSelection(
    std::string_view Prompt,
    std::size_t Maximum
)
{
    if (Maximum >
        static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
    {
        throw std::overflow_error("Console menu is too large");
    }

    return static_cast<std::size_t>(ReadInteger(
        Prompt,
        0,
        static_cast<std::int64_t>(Maximum)
    ));
}
