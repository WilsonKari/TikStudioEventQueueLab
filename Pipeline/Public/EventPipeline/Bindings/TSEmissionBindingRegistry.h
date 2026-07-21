#pragma once

#include "EventPipeline/TikStudioEventPipelineContracts.h"

#include <cstddef>
#include <exception>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>

// Owns the external metadata associated with each globally unique emission identity.
class FTSEmissionBindingRegistry final
{
private:
    using FBindingMap =
        std::unordered_map<FTSEmissionId, FTSEmissionBinding>;

public:
    class FPreparedInsert final
    {
    public:
        FPreparedInsert() = default;

        FPreparedInsert(const FPreparedInsert&) = delete;
        FPreparedInsert& operator=(const FPreparedInsert&) = delete;

        FPreparedInsert(FPreparedInsert&&) noexcept = default;
        FPreparedInsert& operator=(FPreparedInsert&&) noexcept = default;

    private:
        friend class FTSEmissionBindingRegistry;

        explicit FPreparedInsert(FBindingMap::node_type Node) noexcept
            : Node(std::move(Node))
        {
        }

        FBindingMap::node_type Node;
    };

    FTSEmissionBindingRegistry() = default;

    FTSEmissionBindingRegistry(const FTSEmissionBindingRegistry&) = delete;
    FTSEmissionBindingRegistry& operator=(const FTSEmissionBindingRegistry&) = delete;

    FTSEmissionBindingRegistry(FTSEmissionBindingRegistry&&) = delete;
    FTSEmissionBindingRegistry& operator=(FTSEmissionBindingRegistry&&) = delete;

    [[nodiscard]] bool Insert(FTSEmissionBinding Binding)
    {
        std::optional<FPreparedInsert> Prepared =
            PrepareInsert(std::move(Binding));
        if (!Prepared.has_value())
        {
            return false;
        }

        CommitInsert(*Prepared);
        return true;
    }

    // Reserva buckets y construye el nodo antes de cruzar el commit del Core.
    [[nodiscard]]
    std::optional<FPreparedInsert> PrepareInsert(FTSEmissionBinding Binding)
    {
        if (Binding.EmissionId == 0 || Binding.PayloadHandle.Value == 0 ||
            !IsSupportedFamilyFlowPair(
                Binding.FamilyKind,
                Binding.ExpectedFlow
            ) ||
            Binding.ExternalState != ETSExternalEmissionState::Bound ||
            Bindings.contains(Binding.EmissionId))
        {
            return std::nullopt;
        }

        Bindings.reserve(Bindings.size() + 1);

        FBindingMap Staging;
        const auto [Iterator, bInserted] = Staging.emplace(
            Binding.EmissionId,
            std::move(Binding)
        );
        if (!bInserted)
        {
            return std::nullopt;
        }

        return FPreparedInsert(Staging.extract(Iterator));
    }

    // El nodo y la capacidad ya existen; cualquier fallo indica uso doble o una
    // violación de la preparación, no una condición recuperable post-commit.
    void CommitInsert(FPreparedInsert& Prepared) noexcept
    {
        if (Prepared.Node.empty())
        {
            std::terminate();
        }

        const auto InsertResult = Bindings.insert(std::move(Prepared.Node));
        if (!InsertResult.inserted)
        {
            std::terminate();
        }
    }

    template <typename TCallback>
    [[nodiscard]] bool Visit(
        FTSEmissionId EmissionId,
        TCallback&& Callback
    ) const
    {
        const auto Iterator = Bindings.find(EmissionId);
        if (Iterator == Bindings.end())
        {
            return false;
        }

        // The reference is valid only during this callback; it must not be retained or
        // used to mutate this registry reentrantly.
        std::invoke(
            std::forward<TCallback>(Callback),
            static_cast<const FTSEmissionBinding&>(Iterator->second)
        );
        return true;
    }

    template <typename TCallback>
    void VisitAll(TCallback&& Callback) const
    {
        for (const auto& Entry : Bindings)
        {
            std::invoke(
                std::forward<TCallback>(Callback),
                static_cast<const FTSEmissionBinding&>(Entry.second)
            );
        }
    }

    [[nodiscard]] bool TransitionState(
        FTSEmissionId EmissionId,
        ETSExternalEmissionState ExpectedState,
        ETSExternalEmissionState NewState
    ) noexcept
    {
        const auto Iterator = Bindings.find(EmissionId);
        if (Iterator == Bindings.end() ||
            Iterator->second.ExternalState != ExpectedState)
        {
            return false;
        }

        Iterator->second.ExternalState = NewState;
        return true;
    }

    void CommitTransitionState(
        FTSEmissionId EmissionId,
        ETSExternalEmissionState ExpectedState,
        ETSExternalEmissionState NewState
    ) noexcept
    {
        if (!TransitionState(EmissionId, ExpectedState, NewState))
        {
            std::terminate();
        }
    }

    [[nodiscard]] bool Erase(FTSEmissionId EmissionId) noexcept
    {
        return Bindings.erase(EmissionId) == 1;
    }

    void CommitErase(FTSEmissionId EmissionId) noexcept
    {
        if (!Erase(EmissionId))
        {
            std::terminate();
        }
    }

    [[nodiscard]] std::size_t Size() const noexcept
    {
        return Bindings.size();
    }

    [[nodiscard]] bool Empty() const noexcept
    {
        return Bindings.empty();
    }

private:
    // EmissionId is the sole global key; all other fields are routing and verification metadata.
    FBindingMap Bindings;
};
