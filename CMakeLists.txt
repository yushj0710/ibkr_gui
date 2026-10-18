cmake_minimum_required(VERSION 3.24)

project(ibkr_options_gui VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
find_package(Python3 COMPONENTS Interpreter)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# add C:\Qt to CMAKE_PREFIX_PATH if Qt is installed there
# set(CMAKE_PREFIX_PATH "C:/Qt/6.11.2/mingw_64")
# point CMAKE_PREFIX_PATH to the Qt installation, e.g. in CMakeUserPresets.json
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Sql)

qt_standard_project_setup()

set(IBKR_TWS_API_ROOT "C:/TWS API" CACHE PATH "Root directory of the official IBKR TWS API C++ client")
option(IBKR_ENABLE_TWS_API "Build with the official IBKR TWS API C++ client" ON)

if(IBKR_ENABLE_TWS_API AND NOT EXISTS "${IBKR_TWS_API_ROOT}")
    message(FATAL_ERROR "Set IBKR_TWS_API_ROOT to the official TWS API installation directory.")
endif()

qt_add_executable(ibkr_options_gui
    src/main.cpp
    src/domain/ConnectionProfile.h
    src/domain/PortfolioStore.h
    src/domain/PortfolioStore.cpp
    src/domain/ActivityLog.h
    src/domain/ActivityLog.cpp
    src/domain/LocalStore.h
    src/domain/LocalStore.cpp
    src/domain/PortfolioTypes.h
    src/ibkr/IbkrSession.h
    src/ibkr/IbkrSession.cpp
    src/ui/LoginDialog.h
    src/ui/LoginDialog.cpp
    src/ui/LoginDialog.ui
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/OrderTicketDialog.h
    src/ui/OrderTicketDialog.cpp
    src/ui/OrderTicketDialog.ui
    src/ui/models/EquityPositionModel.h
    src/ui/models/EquityPositionModel.cpp
    src/ui/models/OptionPositionModel.h
    src/ui/models/OptionPositionModel.cpp
    src/ui/models/TradeRecordModel.h
    src/ui/models/TradeRecordModel.cpp
    src/ui/models/WatchlistTreeModel.h
    src/ui/models/WatchlistTreeModel.cpp
    src/ui/mainwindow.ui
)

target_link_libraries(ibkr_options_gui PRIVATE Qt6::Widgets Qt6::Sql)
target_include_directories(ibkr_options_gui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(IBKR_ENABLE_TWS_API)
    set(IBKR_TWS_CLIENT_DIR "${IBKR_TWS_API_ROOT}/source/cppclient/client")
    find_library(IBKR_DECIMAL_LIBRARY NAMES intel_decimal128 libintel_decimal128.a bid DOC "Intel decimal floating-point library required by TWS API")
    if(NOT IBKR_DECIMAL_LIBRARY OR NOT EXISTS "${IBKR_DECIMAL_LIBRARY}")
        message(FATAL_ERROR "Set IBKR_DECIMAL_LIBRARY to libintel_decimal128.a from intelrdfpmathlib.")
    endif()
    find_package(absl CONFIG REQUIRED)
    find_package(utf8_range CONFIG REQUIRED)
    # Protobuf's CMake package reports 29.5.0, while its runtime header reports
    # the API-required 5.29.5 as PROTOBUF_VERSION 5029005. Generated IBKR
    # headers enforce that authoritative runtime value during compilation.
    find_package(protobuf CONFIG REQUIRED)
    file(GLOB IBKR_TWS_API_SOURCES CONFIGURE_DEPENDS "${IBKR_TWS_CLIENT_DIR}/*.cpp")
    file(GLOB IBKR_TWS_PROTO_SOURCES CONFIGURE_DEPENDS "${IBKR_TWS_CLIENT_DIR}/protobuf/*.cc" "${IBKR_TWS_CLIENT_DIR}/protobuf/*.cpp")
    add_library(ibkr_twsapi SHARED ${IBKR_TWS_API_SOURCES})
    target_sources(ibkr_twsapi PRIVATE ${IBKR_TWS_PROTO_SOURCES})
    target_include_directories(ibkr_twsapi PUBLIC "${IBKR_TWS_CLIENT_DIR}" "${IBKR_TWS_CLIENT_DIR}/protobuf")
    target_link_libraries(ibkr_twsapi PUBLIC
        ws2_32
        protobuf::libprotobuf
        absl::kernel_timeout_internal
        "${IBKR_DECIMAL_LIBRARY}"
    )
    target_compile_options(ibkr_twsapi PRIVATE -Wno-unused-parameter)
    target_compile_definitions(ibkr_options_gui PRIVATE IBKR_ENABLE_TWS_API=1)
    target_link_libraries(ibkr_options_gui PRIVATE ibkr_twsapi)
endif()

set_target_properties(ibkr_options_gui PROPERTIES
    WIN32_EXECUTABLE TRUE
)

if(WIN32)
    get_filename_component(QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt.exe windeployqt
        HINTS "${QT_PREFIX}/bin"
    )
    if(NOT WINDEPLOYQT_EXECUTABLE)
        message(FATAL_ERROR "windeployqt was not found. Set Qt6_DIR to the Qt installation used for this build.")
    endif()
    # This Qt installation provides release DLLs/plugins only, including for
    # the MinGW configuration used by the debug build.
    set(WINDEPLOYQT_CONFIGURATION --release)
    add_custom_command(TARGET ibkr_options_gui POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            ${WINDEPLOYQT_CONFIGURATION}
            --compiler-runtime
            --no-translations
            "$<TARGET_FILE:ibkr_options_gui>"
        COMMENT "Deploying Qt runtime beside ibkr_options_gui.exe"
        VERBATIM
    )
endif()

install(TARGETS ibkr_options_gui)

if(BUILD_TESTING AND Python3_Interpreter_FOUND)
    add_test(
        NAME python_worker_storage
        COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p "test_*.py"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()