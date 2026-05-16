cmake_minimum_required(VERSION 3.22)

# Silence the FetchContent_Populate deprecation warning
cmake_policy(SET CMP0169 OLD)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_PROJECT_NAME ReflowOven)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

enable_language(C ASM)

add_executable(${CMAKE_PROJECT_NAME})

# 1. DOWNLOAD LIBRARIES
include(FetchContent)

FetchContent_Declare(
  cjson
  URL                        https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.18.zip
  SOURCE_DIR                 ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/Third_Party/cJSON
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_Declare(
  littlefs
  URL                        https://github.com/littlefs-project/littlefs/archive/refs/tags/v2.9.3.zip
  SOURCE_DIR                 ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/Third_Party/littlefs
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Populate folders manually to avoid running their problematic CMakeLists.txt
FetchContent_Populate(cjson)
FetchContent_Populate(littlefs)

# 2. SUBDIRECTORIES
add_subdirectory(cmake/stm32cubemx)

# 3. TARGET SOURCES
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # User Application Sources
    Core/Src/API/APICore.c
    Core/Src/API/APICodec.c
    Core/Src/API/APIHandlers.c
    Core/Src/Drivers/Interrupts.c
    Core/Src/Drivers/I2CManager.c
    Core/Src/Drivers/SPIManager.c
    Core/Src/Drivers/Buzzer.c
    Core/Src/Drivers/DCFan.c
    Core/Src/Drivers/MCU.c
    Core/Src/Drivers/PowerManager.c
    Core/Src/Drivers/RotaryEncoder.c
    Core/Src/Drivers/Thermistor.c
    Core/Src/Drivers/ThermistorI2C.c
    Core/Src/Drivers/Thermocouple.c
    Core/Src/Drivers/Triac.c
    Core/Src/Drivers/USBPowerDelivery.c
    Core/Src/Tasks/APITask.c
    Core/Src/Tasks/DeviceTask.c
    Core/Src/Tasks/LoggingTask.c
    Core/Src/Tasks/ManagerTask.c
    Core/Src/Tasks/USBPDTask.c
    Core/Src/Tasks/TaskUtils.c
    Core/Src/Tuning/ACFanTuning.c

    # Add third-party source files directly to your target
    Middlewares/Third_Party/cJSON/cJSON.c
    Middlewares/Third_Party/littlefs/lfs.c
    Middlewares/Third_Party/littlefs/lfs_util.c
)

# 4. INCLUDE DIRECTORIES
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Inc/API
    Core/Inc/General
    Core/Inc/Tasks
    Core/Inc/Drivers
    Middlewares/Third_Party/cJSON
    Middlewares/Third_Party/littlefs
    Core/Src/Tuning
)

# 5. LINK LIBRARIES
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
)

# 6. REMOVE WRONG LIBOB.A
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)