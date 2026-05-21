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
    Core/Modules/API/Src/APICore.c
    Core/Modules/API/Src/APICodec.c
    Core/Modules/API/Src/APIHandlers.c
    Core/Modules/API/Handlers/Src/DeviceHandler.c
    Core/Modules/API/Handlers/Src/ReflowHandler.c
    Core/Modules/API/Handlers/Src/ProfileHandler.c
    Core/Modules/API/Handlers/Src/ConfigHandler.c
    Core/Modules/API/Handlers/Src/LogsHandler.c
    Core/Modules/API/Handlers/Src/StorageHandler.c
    Core/Modules/API/Handlers/Src/FileHandler.c
    Core/Modules/API/Handlers/Src/SystemHandler.c
    Core/Modules/Drivers/Src/Interrupts.c
    Core/Modules/Drivers/Src/I2CManager.c
    Core/Modules/Drivers/Src/SPIManager.c
    Core/Modules/Drivers/Src/ACFan.c
    Core/Modules/Drivers/Src/ACLight.c
    Core/Modules/Drivers/Src/Buzzer.c
    Core/Modules/Drivers/Src/DCFan.c
    Core/Modules/Drivers/Src/DriverRegistry.c
    Core/Modules/Drivers/Src/Flash.c
    Core/Modules/Drivers/Src/MCU.c
    Core/Modules/Drivers/Src/OvenController.c
    Core/Modules/Drivers/Src/PowerManager.c
    Core/Modules/Drivers/Src/RotaryEncoder.c
    Core/Modules/Drivers/Src/Thermistor.c
    Core/Modules/Drivers/Src/ThermistorI2C.c
    Core/Modules/Drivers/Src/Thermocouple.c
    Core/Modules/Drivers/Src/Triac.c
    Core/Modules/Drivers/Src/USBCDC.c
    Core/Modules/Drivers/Src/USBPowerDelivery.c
    Core/Modules/Tasks/Src/APITask.c
    Core/Modules/Tasks/Src/DeviceTask.c
    Core/Modules/Tasks/Src/LoggingTask.c
    Core/Modules/Tasks/Src/ManagerTask.c
    Core/Modules/Tasks/Src/ReflowTask.c
    Core/Modules/Tasks/Src/USBPDTask.c
    Core/Modules/Tasks/Src/TaskUtils.c
 
    # Calibration
    Core/Modules/Tuning/Src/ACFanTuning.c

    # Reflow
    Core/Modules/Reflow/Src/Reflow.c
    Core/Modules/Reflow/Src/ReflowProfile.c

    # FileSystem
    Core/Modules/FileSystem/Src/BlockDevice.c
    Core/Modules/FileSystem/Src/FlashBlockDevice.c
    Core/Modules/FileSystem/Src/Partition.c
    Core/Modules/FileSystem/Src/LFSAdapter.c
    Core/Modules/FileSystem/Src/Volume.c
    Core/Modules/FileSystem/Src/FSFile.c
    Core/Modules/FileSystem/Src/FSUtils.c

    # Add third-party source files directly to your target
    Middlewares/Third_Party/cJSON/cJSON.c
    Middlewares/Third_Party/littlefs/lfs.c
    Middlewares/Third_Party/littlefs/lfs_util.c
)

# 4. INCLUDE DIRECTORIES
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Modules/API/Inc
    Core/Modules/API/Handlers/Inc
    Core/Modules/Includes
    Core/Modules/Tasks/Inc
    Core/Modules/Drivers/Inc
    Core/Modules/Tuning/Inc
    Core/Modules/Reflow/Inc
    Core/Modules/FileSystem/Inc
    Core/Modules/FileSystem/Src
    Middlewares/Third_Party/cJSON
    Middlewares/Third_Party/littlefs
)

# 5. COMPILE DEFINITIONS
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Redirect third-party library allocations to the FreeRTOS heap so all
    # dynamic memory in the project comes from the same pool.
    CJSON_MALLOC=pvPortMalloc
    CJSON_FREE=vPortFree
    LFS_MALLOC=pvPortMalloc
    LFS_FREE=vPortFree
)

# Force-include FreeRTOS.h before every translation unit so that pvPortMalloc
# and vPortFree are declared before the LFS_MALLOC/LFS_FREE macros expand in
# lfs_util.h, which is pulled in transitively by our filesystem modules.
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-include FreeRTOS.h>
)

# 6. LINK LIBRARIES
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
)

# 6. REMOVE WRONG LIBOB.A
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# 7. DOCUMENTATION
find_package(Doxygen QUIET)
find_package(Python3 QUIET COMPONENTS Interpreter)
if(DOXYGEN_FOUND AND Python3_FOUND)
    add_custom_target(docs ALL
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/gen_api_ref.py
                ${CMAKE_CURRENT_SOURCE_DIR}/Docs/xml
                ${CMAKE_CURRENT_SOURCE_DIR}/Docs/ModuleRef.md
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating Doxygen HTML and Markdown API reference"
        VERBATIM
    )
endif()