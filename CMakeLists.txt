# CMakeList.txt: proyecto de CMake para TikStudioEventQueueLab, incluya el origen y defina
# la lógica específica del proyecto aquí.
#
cmake_minimum_required(VERSION 3.14)

# Habilite Recarga activa para los compiladores de MSVC si se admiten.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project("TikStudioEventQueueLab")

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

find_package(Threads REQUIRED)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
)

FetchContent_MakeAvailable(nlohmann_json)

option(
  TIKSTUDIO_BUILD_TIKFINITY_PROBE
  "Build the manual TikFinity WebSocket probe"
  OFF
)

add_library(TikStudioEventCore STATIC
  Core/Private/EventQueueSystem/TikStudioEventQueueSystem.cpp
  Core/Private/EventQueueSystem/TSEventQueueSystemSettings.cpp
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

add_library(TikStudioEventPipeline STATIC
  Pipeline/Private/EventPipeline/TikStudioEventPipelineContracts.cpp
  Pipeline/Private/EventPipeline/Coordinator/TSEventPipelineCoordinator.cpp
  Pipeline/Private/EventPipeline/Families/TSChatFamily.cpp
  Pipeline/Private/EventPipeline/Families/TSFollowFamily.cpp
  Pipeline/Private/EventPipeline/Families/TSShareFamily.cpp
  Pipeline/Private/EventPipeline/Families/TSLikeFamily.cpp
)

target_include_directories(TikStudioEventPipeline
  PUBLIC
    Pipeline/Public
)

target_link_libraries(TikStudioEventPipeline
  PUBLIC
    TikStudioEventCore
)

add_library(TikStudioEventHost STATIC
  Host/Private/EventHost/TSEventExecutionHost.cpp
)

target_include_directories(TikStudioEventHost
  PUBLIC
    Host/Public
)

target_link_libraries(TikStudioEventHost
  PUBLIC
    TikStudioEventPipeline
  PRIVATE
    Threads::Threads
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
  Adapters/TikFinity/Private/TikFinity/TSTikFinityChatConverter.cpp
  Adapters/TikFinity/Private/TikFinity/TSTikFinityDecodedUserConverter.cpp
  Adapters/TikFinity/Private/TikFinity/TSTikFinityFollowConverter.cpp
  Adapters/TikFinity/Private/TikFinity/TSTikFinityShareConverter.cpp
  Adapters/TikFinity/Private/TikFinity/TSTikFinityLikeConverter.cpp
  Adapters/TikFinity/Private/TikFinity/TSTikFinityJsonEventDecoder.cpp
  Adapters/TikFinity/Private/TikFinity/TSTikFinityMappedEventFormatter.cpp
  Adapters/TikFinity/Private/TikFinity/TSTikFinitySevenEventChecklist.cpp
)

target_include_directories(TikStudioTikFinityAdapter
  PUBLIC
    Adapters/TikFinity/Public
  PRIVATE
    Adapters/TikFinity
    Adapters/TikFinity/Private
)

target_link_libraries(TikStudioTikFinityAdapter
  PUBLIC
    TikStudioEventCore
  PRIVATE
    nlohmann_json::nlohmann_json
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

add_executable(TikStudioEventCoreTests
  Tests/TikStudioEventQueueSystemTests.cpp
)

target_link_libraries(TikStudioEventCoreTests
  PRIVATE
    TikStudioEventCore
)

add_test(
  NAME TikStudioEventCoreTests
  COMMAND TikStudioEventCoreTests
)

add_executable(TikStudioEventPipelineTests
  Tests/TikStudioEventPipelineTests.cpp
  Tests/TSPipelineInfrastructureTests.cpp
  Tests/Chat/TSChatPipelineTests.cpp
  Tests/Follow/TSFollowPipelineTests.cpp
  Tests/Share/TSSharePipelineTests.cpp
  Tests/Like/TSLikePipelineTests.cpp
)

target_include_directories(TikStudioEventPipelineTests
  PRIVATE
    Tests
)

target_link_libraries(TikStudioEventPipelineTests
  PRIVATE
    TikStudioEventPipeline
)

add_test(
  NAME TikStudioEventPipelineTests
  COMMAND TikStudioEventPipelineTests
)

add_executable(TikStudioEventHostTests
  Tests/TikStudioEventHostTests.cpp
  Tests/Chat/TSChatHostTests.cpp
  Tests/Follow/TSFollowHostTests.cpp
  Tests/Share/TSShareHostTests.cpp
)

target_include_directories(TikStudioEventHostTests
  PRIVATE
    Tests
)

target_link_libraries(TikStudioEventHostTests
  PRIVATE
    TikStudioEventHost
)

add_test(
  NAME TikStudioEventHostTests
  COMMAND TikStudioEventHostTests
)

add_executable(TikStudioVerticalIntegrationTests
  Tests/TikStudioVerticalIntegrationTests.cpp
  Tests/Chat/TSChatVerticalIntegrationTests.cpp
  Tests/Follow/TSFollowVerticalIntegrationTests.cpp
  Tests/Share/TSShareVerticalIntegrationTests.cpp
)

target_include_directories(TikStudioVerticalIntegrationTests
  PRIVATE
    Tests
)

target_link_libraries(TikStudioVerticalIntegrationTests
  PRIVATE
    TikStudioEventHost
    TikStudioTikFinityAdapter
)

add_test(
  NAME TikStudioVerticalIntegrationTests
  COMMAND TikStudioVerticalIntegrationTests
)

add_executable(TikStudioTikFinityAdapterTests
  Tests/TikStudioTikFinityAdapterTests.cpp
  Tests/Chat/TSTikFinityChatAdapterTests.cpp
  Tests/Follow/TSTikFinityFollowAdapterTests.cpp
  Tests/Share/TSTikFinityShareAdapterTests.cpp
  Tests/Like/TSTikFinityLikeAdapterTests.cpp
)

target_include_directories(TikStudioTikFinityAdapterTests
  PRIVATE
    Tests
)

target_link_libraries(TikStudioTikFinityAdapterTests
  PRIVATE
    TikStudioTikFinityAdapter
)

add_test(
  NAME TikStudioTikFinityAdapterTests
  COMMAND TikStudioTikFinityAdapterTests
)

add_executable(TikStudioTikFinityJsonDecoderTests
  Tests/TikStudioTikFinityJsonDecoderTests.cpp
)

target_link_libraries(TikStudioTikFinityJsonDecoderTests
  PRIVATE
    TikStudioTikFinityAdapter
)

add_test(
  NAME TikStudioTikFinityJsonDecoderTests
  COMMAND TikStudioTikFinityJsonDecoderTests
)

add_executable(TikStudioTikFinityChecklistTests
  Tests/TikStudioTikFinityChecklistTests.cpp
)

target_link_libraries(TikStudioTikFinityChecklistTests
  PRIVATE
    TikStudioTikFinityAdapter
)

add_test(
  NAME TikStudioTikFinityChecklistTests
  COMMAND TikStudioTikFinityChecklistTests
)

if(TIKSTUDIO_BUILD_TIKFINITY_PROBE)
  set(BUILD_DEMO OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(USE_TLS OFF CACHE BOOL "" FORCE)
  set(USE_ZLIB OFF CACHE BOOL "" FORCE)
  set(USE_WS OFF CACHE BOOL "" FORCE)
  set(USE_TEST OFF CACHE BOOL "" FORCE)
  set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG v12.0.1
  )

  FetchContent_MakeAvailable(ixwebsocket)

  add_executable(TikStudioTikFinitySevenEventProbe
    Tools/TikFinitySevenEventProbe/main.cpp
  )

  target_link_libraries(TikStudioTikFinitySevenEventProbe
    PRIVATE
      TikStudioTikFinityAdapter
      ixwebsocket::ixwebsocket
      Threads::Threads
  )
endif()
