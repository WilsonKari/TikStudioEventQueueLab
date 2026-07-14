# CMakeList.txt: proyecto de CMake para TikStudioEventQueueLab, incluya el origen y defina
# la lógica específica del proyecto aquí.
#
cmake_minimum_required(VERSION 3.12)

# Habilite Recarga activa para los compiladores de MSVC si se admiten.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project("TikStudioEventQueueLab")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(TikStudioEventCore STATIC
  Core/Private/EventQueueSystem/TikStudioEventQueueSystem.cpp
  Core/Private/EventQueueSystem/Events/TikStudioEventQueueSystem_Chat.cpp
  Core/Private/EventQueueSystem/Events/TikStudioEventQueueSystem_Gift.cpp
  Core/Private/EventQueueSystem/Events/TikStudioEventQueueSystem_Like.cpp
  Core/Private/EventQueueSystem/Events/TikStudioEventQueueSystem_Member.cpp
  Core/Private/EventQueueSystem/Events/TikStudioEventQueueSystem_RoomUser.cpp
  Core/Private/EventQueueSystem/Events/TikStudioEventQueueSystem_Share.cpp
  Core/Private/EventQueueSystem/Events/TikStudioEventQueueSystem_Follow.cpp
)

target_include_directories(TikStudioEventCore
  PUBLIC
    Core/Public
)

add_library(TikStudioEventSimulator STATIC
  Adapters/Simulator/TikStudioEventSimulator.cpp
)

target_include_directories(TikStudioEventSimulator
  PUBLIC
    Adapters/Simulator
)

target_link_libraries(TikStudioEventSimulator
  PUBLIC
    TikStudioEventCore
)

add_library(TikStudioTikFinityAdapter STATIC
  Adapters/TikFinity/TikStudioTikFinityClient.cpp
)

target_include_directories(TikStudioTikFinityAdapter
  PUBLIC
    Adapters/TikFinity
)

target_link_libraries(TikStudioTikFinityAdapter
  PUBLIC
    TikStudioEventCore
)

add_executable(TikStudioEventConsole
  Console/main.cpp
)

target_link_libraries(TikStudioEventConsole
  PRIVATE
    TikStudioEventCore
    TikStudioEventSimulator
    TikStudioTikFinityAdapter
)
