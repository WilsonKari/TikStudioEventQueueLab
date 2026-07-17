#pragma once

#include "TikFinity/TSTikFinityMappedEventContracts.h"

#include <string>

// Representación diagnóstica determinista de los campos mapeados del adaptador.
class FTSTikFinityMappedEventFormatter final
{
public:
    [[nodiscard]]
    static std::string Format(const FTSTikFinityMappedEvent& Event);
};
