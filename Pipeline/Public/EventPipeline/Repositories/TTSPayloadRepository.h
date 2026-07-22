#pragma once

#include "EventPipeline/TikStudioEventPipelineContracts.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <type_traits>

template <typename TPayload>
class TTSPayloadRepository final
{
public:
    class FPreparedReplace final
    {
    public:
        FPreparedReplace() = default;

        FPreparedReplace(const FPreparedReplace&) = delete;
        FPreparedReplace& operator=(const FPreparedReplace&) = delete;

        FPreparedReplace(FPreparedReplace&& Other) noexcept(
            std::is_nothrow_move_constructible_v<TPayload>
        )
            : Owner(std::exchange(Other.Owner, nullptr))
            , Handle(std::exchange(Other.Handle, FTSPayloadHandle{}))
            , Payload(std::move(Other.Payload))
        {
            Other.Payload.reset();
        }

        FPreparedReplace& operator=(FPreparedReplace&&) = delete;

    private:
        friend class TTSPayloadRepository;

        FPreparedReplace(
            const TTSPayloadRepository* Owner,
            FTSPayloadHandle Handle,
            TPayload Payload
        )
            : Owner(Owner)
            , Handle(Handle)
            , Payload(std::move(Payload))
        {
        }

        const TTSPayloadRepository* Owner = nullptr;
        FTSPayloadHandle Handle{};
        std::optional<TPayload> Payload;
    };

    TTSPayloadRepository() = default;

    TTSPayloadRepository(const TTSPayloadRepository&) = delete;
    TTSPayloadRepository& operator=(const TTSPayloadRepository&) = delete;

    TTSPayloadRepository(TTSPayloadRepository&&) = delete;
    TTSPayloadRepository& operator=(TTSPayloadRepository&&) = delete;

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

        // La referencia sólo es válida durante el callback: no debe conservarse fuera
        // de la llamada ni usarse para modificar reentrantemente esta instancia.
        std::invoke(
            std::forward<TCallback>(Callback),
            static_cast<const TPayload&>(PayloadIt->second)
        );
        return true;
    }

    // La copia sustituta se construye por completo antes del commit; el payload
    // autoritativo permanece intacto si la preparación falla o se abandona.
    [[nodiscard]]
    std::optional<FPreparedReplace> PrepareReplace(
        FTSPayloadHandle Handle,
        TPayload Payload
    ) const
    {
        if (Handle.Value == 0 || !Payloads.contains(Handle.Value))
        {
            return std::nullopt;
        }

        return FPreparedReplace(this, Handle, std::move(Payload));
    }

    void CommitReplace(FPreparedReplace& Prepared) noexcept
    {
        static_assert(std::is_nothrow_swappable_v<TPayload>);

        if (Prepared.Owner != this || !Prepared.Payload.has_value())
        {
            std::terminate();
        }

        const auto PayloadIt = Payloads.find(Prepared.Handle.Value);
        if (PayloadIt == Payloads.end())
        {
            std::terminate();
        }

        using std::swap;
        swap(PayloadIt->second, *Prepared.Payload);
        Prepared.Payload.reset();
        Prepared.Owner = nullptr;
    }

    [[nodiscard]]
    bool Erase(FTSPayloadHandle Handle) noexcept
    {
        if (Handle.Value == 0)
        {
            return false;
        }

        return Payloads.erase(Handle.Value) == 1;
    }

    void CommitErase(FTSPayloadHandle Handle) noexcept
    {
        if (!Erase(Handle))
        {
            std::terminate();
        }
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
    // El repositorio sólo posee payloads; desconoce si la coordinación externa los
    // considera provisionales, vinculados o terminales.
    std::unordered_map<std::uint64_t, TPayload> Payloads;
};
