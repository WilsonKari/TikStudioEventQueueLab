#pragma once

#include "EventPipeline/TikStudioEventPipelineContracts.h"

#include <cstddef>
#include <functional>
#include <unordered_map>
#include <utility>

// Owns the external metadata associated with each globally unique emission identity.
class FTSEmissionBindingRegistry final
{
public:
    [[nodiscard]] bool Insert(FTSEmissionBinding Binding)
    {
        if (Binding.EmissionId == 0 || Binding.PayloadHandle.Value == 0 ||
            !IsValidFlow(Binding.ExpectedFlow))
        {
            return false;
        }

        return Bindings.emplace(Binding.EmissionId, std::move(Binding)).second;
    }

    template <typename TCallback>
    [[nodiscard]] bool Visit(FTSEmissionId EmissionId, TCallback&& Callback) const
    {
        const auto Iterator = Bindings.find(EmissionId);
        if (Iterator == Bindings.end())
        {
            return false;
        }

        // The binding remains registry-owned; callers receive a read-only view only for this call.
        std::invoke(
            std::forward<TCallback>(Callback),
            static_cast<const FTSEmissionBinding&>(Iterator->second));
        return true;
    }

    [[nodiscard]] bool TransitionState(
        FTSEmissionId EmissionId,
        ETSExternalEmissionState ExpectedState,
        ETSExternalEmissionState NewState)
    {
        const auto Iterator = Bindings.find(EmissionId);
        if (Iterator == Bindings.end() || Iterator->second.ExternalState != ExpectedState)
        {
            return false;
        }

        Iterator->second.ExternalState = NewState;
        return true;
    }

    [[nodiscard]] bool Erase(FTSEmissionId EmissionId)
    {
        return Bindings.erase(EmissionId) == 1;
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
    std::unordered_map<FTSEmissionId, FTSEmissionBinding> Bindings;
};
