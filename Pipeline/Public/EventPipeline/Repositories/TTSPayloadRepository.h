#pragma once

#include "EventPipeline/TikStudioEventPipelineContracts.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

template <typename TPayload>
class TTSPayloadRepository final
{
public:
    [[nodiscard]]
    std::optional<FTSPayloadHandle> Insert(TPayload Payload)
    {
        if (NextHandleValue == 0)
        {
            return std::nullopt;
        }

        const std::uint64_t HandleValue = NextHandleValue;
        const bool bInserted = Payloads.emplace(
            HandleValue,
            std::move(Payload)
        ).second;

        if (!bInserted)
        {
            throw std::logic_error("Duplicate payload handle");
        }

        // El contador sólo avanza tras adquirir ownership. Cero conserva el
        // agotamiento y evita reutilizar identidades durante la vida de la instancia.
        if (HandleValue == std::numeric_limits<std::uint64_t>::max())
        {
            NextHandleValue = 0;
        }
        else
        {
            ++NextHandleValue;
        }

        return FTSPayloadHandle{HandleValue};
    }

    template <typename TCallback>
    [[nodiscard]]
    bool Visit(
        FTSPayloadHandle Handle,
        TCallback&& Callback
    ) const
    {
        if (Handle.Value == 0)
        {
            return false;
        }

        const auto PayloadIt = Payloads.find(Handle.Value);

        if (PayloadIt == Payloads.end())
        {
            return false;
        }

        // La referencia es válida sólo durante el callback; el repositorio nunca
        // expone punteros o referencias que sobrevivan a futuras mutaciones.
        std::invoke(
            std::forward<TCallback>(Callback),
            static_cast<const TPayload&>(PayloadIt->second)
        );
        return true;
    }

    [[nodiscard]]
    bool Erase(FTSPayloadHandle Handle)
    {
        if (Handle.Value == 0)
        {
            return false;
        }

        return Payloads.erase(Handle.Value) == 1;
    }

    [[nodiscard]]
    std::size_t Size() const noexcept
    {
        return Payloads.size();
    }

    [[nodiscard]]
    bool Empty() const noexcept
    {
        return Payloads.empty();
    }

private:
    std::uint64_t NextHandleValue = 1;
    std::unordered_map<std::uint64_t, TPayload> Payloads;
};
