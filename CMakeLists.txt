cmake_minimum_required(VERSION 3.21)

project(NetAnomalyAnalyzer
    VERSION 1.0.0
    DESCRIPTION "Анализатор аномалий в сети (Qt + Npcap/libpcap + LLM)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

# --- Детект поддержки захвата (Npcap на Windows / libpcap на POSIX) --------
option(NETAN_WITH_PCAP "Собрать с поддержкой реального захвата pcap" ON)
set(NPCAP_SDK_DIR "" CACHE PATH
    "Каталог Npcap SDK (содержит Include/ и Lib/x64/). Нужен для Windows.")

set(PCAP_FOUND OFF)
if(NETAN_WITH_PCAP)
    # Порядок: официальный SDK (если задан) -> локальный минимальный SDK в
    # third_party/npcap (заголовок по ABI WinPcap + импортная библиотека,
    # сгенерированная из установленной System32\wpcap.dll, см. README).
    # third_party/npcap — только для Windows: на POSIX это были бы
    # несовместимые с системой файлы (импортная библиотека PE-формата).
    set(NETAN_NPCAP_HINTS "")
    if(WIN32)
        set(NETAN_NPCAP_HINTS
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/npcap/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/npcap/lib")
    endif()

    find_path(PCAP_INCLUDE_DIR
        NAMES pcap.h
        HINTS
            "${NPCAP_SDK_DIR}/Include"
            "$ENV{NPCAP_SDK_DIR}/Include"
            "C:/Npcap-SDK/Include"
            "C:/npcap/Include"
            ${NETAN_NPCAP_HINTS}
        PATHS /usr/include /usr/local/include /opt/homebrew/include)

    find_library(PCAP_LIBRARY
        NAMES wpcap pcap
        HINTS
            "${NPCAP_SDK_DIR}/Lib/x64"
            "${NPCAP_SDK_DIR}/Lib"
            "$ENV{NPCAP_SDK_DIR}/Lib/x64"
            "C:/Npcap-SDK/Lib/x64"
            "C:/npcap/Lib/x64"
            ${NETAN_NPCAP_HINTS}
        PATHS /usr/lib /usr/local/lib /opt/homebrew/lib)

    if(PCAP_INCLUDE_DIR AND PCAP_LIBRARY)
        set(PCAP_FOUND ON)
        message(STATUS "pcap найден: ${PCAP_LIBRARY}")
    else()
        message(WARNING
            "pcap не найден — будет собрана демонстрационная версия "
            "со симулятором трафика. Укажите -DNPCAP_SDK_DIR=путь_к_SDK "
            "для реального захвата (скачать: https://npcap.com/#download).")
    endif()
endif()

# --- Ядро (без графической оболочки) ----------------------------------------
# Статическая библиотека со всем функционалом: захват, детект аномалий,
# статистика, журналы, оповещения, LLM, настройки. Не зависит от QtWidgets.
# Её используют три оболочки:
#   * NetAnomalyAnalyzer — графическая (Qt Widgets);
#   * NetAnalyzerService  — консольная/служба Windows (без GUI);
#   * FilterTest          — автотесты.
set(CORE_SOURCES
    src/app/AppController.h
    src/app/AppController.cpp

    src/core/Packet.h
    src/core/Packet.cpp
    src/core/PacketParser.h
    src/core/PacketParser.cpp
    src/core/FilterCondition.h
    src/core/FilterCondition.cpp
    src/core/PacketGrouper.h
    src/core/PacketGrouper.cpp
    src/core/ICaptureEngine.h
    src/core/ICaptureEngine.cpp
    src/core/PcapCaptureEngine.h
    src/core/PcapCaptureEngine.cpp
    src/core/SimulationCaptureEngine.h
    src/core/SimulationCaptureEngine.cpp
    src/core/CaptureDevice.h
    src/core/CaptureDevice.cpp
    src/core/InterfaceManager.h
    src/core/InterfaceManager.cpp

    src/anomaly/Anomaly.h
    src/anomaly/Anomaly.cpp
    src/anomaly/RateWindow.h
    src/anomaly/RateWindow.cpp
    src/anomaly/AnomalyRule.h
    src/anomaly/AnomalyRule.cpp
    src/anomaly/PortScanRule.h
    src/anomaly/PortScanRule.cpp
    src/anomaly/SynFloodRule.h
    src/anomaly/SynFloodRule.cpp
    src/anomaly/UdpFloodRule.h
    src/anomaly/UdpFloodRule.cpp
    src/anomaly/IcmpFloodRule.h
    src/anomaly/IcmpFloodRule.cpp
    src/anomaly/BroadcastStormRule.h
    src/anomaly/BroadcastStormRule.cpp
    src/anomaly/SuspiciousPortRule.h
    src/anomaly/SuspiciousPortRule.cpp
    src/anomaly/PayloadSignatureRule.h
    src/anomaly/PayloadSignatureRule.cpp
    src/anomaly/AnomalyDetector.h
    src/anomaly/AnomalyDetector.cpp
    src/anomaly/AnomalyStatusStore.h
    src/anomaly/AnomalyStatusStore.cpp

    src/logging/LogWriter.h
    src/logging/LogWriter.cpp

    src/llm/LlmClient.h
    src/llm/LlmClient.cpp
    src/llm/OpenAiCompatibleClient.h
    src/llm/OpenAiCompatibleClient.cpp
    src/llm/LlmJournalEntry.h
    src/llm/LlmJournalEntry.cpp
    src/llm/LlmJournal.h
    src/llm/LlmJournal.cpp
    src/llm/LlmVerdict.h
    src/llm/LlmVerdict.cpp
    src/llm/PromptBudget.h
    src/llm/PromptBudget.cpp
    src/llm/LlmSentStore.h
    src/llm/LlmSentStore.cpp
    src/llm/LlmScheduler.h
    src/llm/LlmScheduler.cpp

    src/notify/Notifier.h
    src/notify/Notifier.cpp
    src/notify/LogNotifier.h
    src/notify/LogNotifier.cpp
    src/notify/TelegramNotifier.h
    src/notify/TelegramNotifier.cpp
    src/notify/WebhookNotifier.h
    src/notify/WebhookNotifier.cpp
    src/notify/EmailNotifier.h
    src/notify/EmailNotifier.cpp
    src/notify/NotificationManager.h
    src/notify/NotificationManager.cpp

    src/stats/StatisticsCollector.h
    src/stats/StatisticsCollector.cpp

    src/settings/Settings.h
    src/settings/Settings.cpp
)

add_library(NetAnalyzerCore STATIC ${CORE_SOURCES})
target_include_directories(NetAnalyzerCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(NetAnalyzerCore PUBLIC Qt6::Core Qt6::Network)
if(PCAP_FOUND)
    target_compile_definitions(NetAnalyzerCore PRIVATE HAVE_PCAP)
    target_include_directories(NetAnalyzerCore PRIVATE ${PCAP_INCLUDE_DIR})
    target_link_libraries(NetAnalyzerCore PRIVATE ${PCAP_LIBRARY})
    if(WIN32)
        target_link_libraries(NetAnalyzerCore PRIVATE ws2_32)
    endif()
endif()

# --- Графическая оболочка ----------------------------------------------------
set(UI_SOURCES
    src/main.cpp

    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/PacketTableModel.h
    src/ui/PacketTableModel.cpp
    src/ui/PacketDetailsDialog.h
    src/ui/PacketDetailsDialog.cpp
    src/ui/FilterDialog.h
    src/ui/FilterDialog.cpp
    src/ui/PacketFilterProxy.h
    src/ui/PacketFilterProxy.cpp
    src/ui/GroupingWidget.h
    src/ui/GroupingWidget.cpp
    src/ui/TableSorter.h
    src/ui/TableSorter.cpp
    src/ui/AnomalyTableModel.h
    src/ui/AnomalyTableModel.cpp
    src/ui/LlmJournalModel.h
    src/ui/LlmJournalModel.cpp
    src/ui/TimeSeriesWidget.h
    src/ui/TimeSeriesWidget.cpp
    src/ui/ProtocolBarWidget.h
    src/ui/ProtocolBarWidget.cpp
    src/ui/StatsWidget.h
    src/ui/StatsWidget.cpp
    src/ui/LogViewerWidget.h
    src/ui/LogViewerWidget.cpp
    src/ui/TimelineWidget.h
    src/ui/TimelineWidget.cpp
    src/ui/SettingsWidget.h
    src/ui/SettingsWidget.cpp
    src/ui/InterfacesSettingsWidget.h
    src/ui/InterfacesSettingsWidget.cpp
    src/ui/DetectionSettingsWidget.h
    src/ui/DetectionSettingsWidget.cpp
    src/ui/LlmSettingsWidget.h
    src/ui/LlmSettingsWidget.cpp
    src/ui/NotificationSettingsWidget.h
    src/ui/NotificationSettingsWidget.cpp
    src/ui/LoggingSettingsWidget.h
    src/ui/LoggingSettingsWidget.cpp
    src/ui/HelpDialog.h
    src/ui/HelpDialog.cpp
    resources/app.qrc
    resources/manual.qrc
)

# --- Справка -----------------------------------------------------------------
# HTML-версия (help_src/manual.md -> resources/manual*.html) встроится в exe
# через manual.qrc (полная страница, оглавление и темы — отдельными файлами
# для двухколоночного окна справки); настоящий WinHelp-файл кладётся рядом
# с программой (help/netanalyzer.hlp) — откроется, если есть WinHlp32.

# Пересборка справки из исходника (help_src/manual.md) — при наличии Python.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(HelpRegen
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_help.py"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Пересборка справки (.hlp + manual*.html) из help_src/manual.md")
endif()

# --- Иконка приложения ------------------------------------------------------
set(APP_ICON_FILES "")
if(WIN32)
    enable_language(RC)
    # .rc со ссылкой на иконку, сгенерируемая в каталог сборки с абсолютным
    # путём — так windres/rc находит файл при любой вложенности сборки.
    set(APP_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc")
    file(GENERATE OUTPUT "${APP_ICON_RC}"
        CONTENT "IDI_APP_ICON ICON \"${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico\"\n")
    list(APPEND APP_ICON_FILES "${APP_ICON_RC}")
endif()

if(WIN32)
    add_executable(NetAnomalyAnalyzer WIN32 ${UI_SOURCES} ${APP_ICON_FILES})
else()
    add_executable(NetAnomalyAnalyzer ${UI_SOURCES})
endif()

target_link_libraries(NetAnomalyAnalyzer PRIVATE NetAnalyzerCore Qt6::Widgets)

# Справка рядом с программой: help/netanalyzer.hlp (откроется в WinHlp32).
add_custom_command(TARGET NetAnomalyAnalyzer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:NetAnomalyAnalyzer>/help"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/netanalyzer.hlp"
        "$<TARGET_FILE_DIR:NetAnomalyAnalyzer>/help/netanalyzer.hlp"
    COMMENT "Копирование справки netanalyzer.hlp рядом с программой")

# --- Консольная/служебная оболочка (без GUI) --------------------------------
add_executable(NetAnalyzerService src/service_main.cpp)
target_link_libraries(NetAnalyzerService PRIVATE NetAnalyzerCore)
if(WIN32)
    # SCM API (OpenSCManager/CreateService/StartServiceCtrlDispatcher)
    target_link_libraries(NetAnalyzerService PRIVATE advapi32)
endif()

# --- Самодиагностика pcap (консольная утилита) ------------------------------
if(PCAP_FOUND)
    add_executable(PcapSelfTest tools/pcap_selftest.cpp)
    target_include_directories(PcapSelfTest PRIVATE ${PCAP_INCLUDE_DIR})
    target_link_libraries(PcapSelfTest PRIVATE ${PCAP_LIBRARY})
endif()

# --- Автотест фильтрации пакетов (FilterCondition + PacketFilterProxy) ------
# Ядро берётся из библиотеки NetAnalyzerCore, здесь — только GUI-части теста.
add_executable(FilterTest
    tools/filter_test.cpp
    src/ui/PacketTableModel.cpp
    src/ui/PacketFilterProxy.cpp
    src/ui/AnomalyTableModel.cpp
    src/ui/GroupingWidget.cpp
    src/ui/TableSorter.cpp
    src/ui/LogViewerWidget.cpp
    src/ui/TimelineWidget.cpp
    src/ui/FilterDialog.cpp
)
target_link_libraries(FilterTest PRIVATE NetAnalyzerCore Qt6::Widgets)

# Русская локализация Qt (если доступна)
find_package(Qt6 COMPONENTS LinguistTools QUIET)

# --- Установка и пакетирование -----------------------------------------------
# Пакеты (см. README, раздел «Сборка пакетов»):
#   * Windows — MSI (CPack + WiX): Qt DLL, плагины и рантайм компилятора
#     подтягивает windeployqt; настройки и журналы приложение само создаёт
#     в %ProgramData%\NetAnalyzer (конфиги и журналы общие для пользователей).
#   * Linux — DEB и RPM (CPack): пакет содержит свои Qt-библиотеки и плагины
#     (самодостаточен, не зависит от версии Qt в системе); бинарники в
#     /usr/lib/netanalyzer + симлинк /usr/bin/netanalyzer, конфигурация —
#     /etc/netanalyzer, журналы — /var/log/netanalyzer (каталоги создаёт
#     сам пакет при установке).

if(UNIX AND NOT APPLE)
    # Стандартный для Linux префикс: пакеты ставят в /usr (при CPACK_SET_DESTDIR
    # значение CMAKE_INSTALL_PREFIX и определяет, куда попадут относительные
    # пути; переопределить можно флагом -DCMAKE_INSTALL_PREFIX=...).
    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Каталог установки" FORCE)
    endif()

    set(NETAN_INSTALL_LIBDIR "lib/netanalyzer")
else()
    set(NETAN_INSTALL_LIBDIR ".")
endif()

install(TARGETS NetAnomalyAnalyzer NetAnalyzerService
    RUNTIME DESTINATION ${NETAN_INSTALL_LIBDIR})

if(WIN32)
    # --- Windows: Qt через windeployqt --------------------------------------
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
                   "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/License.txt"
        DESTINATION .)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/netanalyzer.hlp"
        DESTINATION help)

    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
        HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXECUTABLE)
        install(CODE "
            message(STATUS \"windeployqt: копирование Qt DLL, плагинов и рантайма компилятора\")
            execute_process(
                COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                        --no-translations
                        --no-system-d3d-compiler
                        \"\${CMAKE_INSTALL_PREFIX}/NetAnomalyAnalyzer.exe\"
                RESULT_VARIABLE _wd_result)
            if(NOT _wd_result EQUAL 0)
                message(FATAL_ERROR \"windeployqt: ошибка \${_wd_result}\")
            endif()
        ")
    else()
        message(WARNING "windeployqt не найден — Qt DLL не попадут в установку")
    endif()

elseif(UNIX AND NOT APPLE)
    # --- Linux: самодостаточный запуск (Qt-библиотеки рядом) ----------------
    # RUNPATH $ORIGIN: exe находит Qt рядом с собой.
    set_target_properties(NetAnomalyAnalyzer NetAnalyzerService PROPERTIES
        INSTALL_RPATH "$ORIGIN")

    # Qt-библиотеки копируются в пакет целиком (включая симлинки SONAME).
    get_target_property(_qt_core_loc Qt6::Core IMPORTED_LOCATION)
    get_filename_component(_qt_lib_dir "${_qt_core_loc}" DIRECTORY)
    set(NETAN_QT_LIB_NAMES
        libQt6Core libQt6Gui libQt6Widgets libQt6Network
        libQt6DBus libQt6XcbQpa libQt6OpenGL libQt6OpenGLWidgets)

    # Каталог плагинов Qt (раскладка Debian/Ubuntu); если его нет — просто
    # пропускаем копирование плагинов, пакет всё равно соберётся.
    set(_qt_plugin_dir "${_qt_lib_dir}/qt6/plugins")
    if(NOT EXISTS "${_qt_plugin_dir}")
        set(_qt_plugin_dir "")
        message(WARNING "Каталог плагинов Qt не найден: ${_qt_lib_dir}/qt6/plugins")
    endif()

    find_program(PATCHELF_EXECUTABLE patchelf)
    install(CODE "
        set(_dest \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${NETAN_INSTALL_LIBDIR}\")
        file(MAKE_DIRECTORY \"\${_dest}\")

        # Qt-библиотеки. Внимание: имя списка подставляется на этапе
        # конфигурации (IN ITEMS ${...}), т.к. переменная недоступна в
        # скрипте установки.
        foreach(_name IN ITEMS ${NETAN_QT_LIB_NAMES})
            file(GLOB _files \"${_qt_lib_dir}/\${_name}.so*\")
            if(_files)
                file(COPY \${_files} DESTINATION \"\${_dest}\")
            endif()
        endforeach()

        # Плагины Qt: платформа (xcb), ввод, форматы изображений, TLS, сеть.
        if(NOT \"${_qt_plugin_dir}\" STREQUAL \"\")
            foreach(_plug IN ITEMS platforms platforminputcontexts imageformats
                                   tls networkinformation generic styles)
                if(EXISTS \"${_qt_plugin_dir}/\${_plug}\")
                    file(COPY \"${_qt_plugin_dir}/\${_plug}\" DESTINATION \"\${_dest}\")
                endif()
            endforeach()
        endif()

        # RUNPATH: библиотеки ищут соседей в своём каталоге, плагины в
        # каталоге выше (иначе xcb-платформа не найдёт libQt6XcbQpa).
        if(\"${PATCHELF_EXECUTABLE}\" STREQUAL \"\")
            message(WARNING \"patchelf не найден — RUNPATH Qt-библиотек не выставлен\")
        else()
            file(GLOB_RECURSE _elfs \"\${_dest}/*.so*\")
            foreach(_elf IN LISTS _elfs)
                if(NOT IS_SYMLINK \"\${_elf}\")
                    get_filename_component(_parent \"\${_elf}\" DIRECTORY)
                    if(_parent STREQUAL _dest)
                        execute_process(COMMAND \"${PATCHELF_EXECUTABLE}\"
                            --set-rpath \"\$ORIGIN\" \"\${_elf}\")
                    else()
                        execute_process(COMMAND \"${PATCHELF_EXECUTABLE}\"
                            --set-rpath \"\$ORIGIN/..\" \"\${_elf}\")
                    endif()
                endif()
            endforeach()
        endif()
    ")

    # Симлинки в /usr/bin (desktop-файл ссылается на «netanalyzer»).
    install(CODE "
        set(_usr_bin \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin\")
        file(MAKE_DIRECTORY \"\${_usr_bin}\")
        file(REMOVE \"\${_usr_bin}/netanalyzer\" \"\${_usr_bin}/netanalyzer-service\")
        file(CREATE_LINK \"../lib/netanalyzer/NetAnomalyAnalyzer\"
             \"\${_usr_bin}/netanalyzer\" SYMBOLIC)
        file(CREATE_LINK \"../lib/netanalyzer/NetAnalyzerService\"
             \"\${_usr_bin}/netanalyzer-service\" SYMBOLIC)
    ")

    # Машинные каталоги конфигурации и журналов (пути абсолютные, поэтому
    # к ним CMAKE_INSTALL_PREFIX не применяется — только DESTDIR).
    install(CODE "
        file(MAKE_DIRECTORY \"\$ENV{DESTDIR}/etc/netanalyzer\")
        file(MAKE_DIRECTORY \"\$ENV{DESTDIR}/var/log/netanalyzer\")
    ")

    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/netanalyzer.desktop"
        DESTINATION share/applications)
    # packaging/linux/icons/hicolor/... -> share/icons/hicolor/...
    # (завершающий слэш — копируется содержимое каталога icons).
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/icons/"
        DESTINATION share/icons)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        DESTINATION share/doc/netanalyzer)
endif()

# --- CPack: MSI (Windows) / DEB+RPM (Linux) ----------------------------------
set(CPACK_PACKAGE_NAME "NetAnomalyAnalyzer")
set(CPACK_PACKAGE_VENDOR "NetAnalyzer")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "NetAnalyzer")

if(WIN32)
    set(CPACK_GENERATOR "WIX")
    set(CPACK_PACKAGE_FILE_NAME "netanomalyanalyzer-${PROJECT_VERSION}-win64")
    # Стабильный GUID продукта: установка новой версии заменяет старую.
    set(CPACK_WIX_UPGRADE_GUID "{0FEBB49E-DA8D-49AD-81A2-B470B8D2749F}")
    set(CPACK_WIX_PRODUCT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico")
    # Установка для всех пользователей (Program Files, запись в ProgramData).
    set(CPACK_WIX_INSTALL_SCOPE "perMachine")
    # Лицензионное уведомление (Qt LGPL и др.) на странице установщика.
    set(CPACK_RESOURCE_FILE_LICENSE
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/License.txt")
    # Ярлыки: меню «Пуск» (запуск + удаление добавляется автоматически)
    # и рабочий стол. Подписи — ASCII: язык MSI по умолчанию en-US (1252).
    set(CPACK_PACKAGE_EXECUTABLES
        "NetAnomalyAnalyzer" "Net Anomaly Analyzer")
    set(CPACK_CREATE_DESKTOP_LINKS "NetAnomalyAnalyzer")
elseif(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB;RPM")
    set(CPACK_PACKAGE_NAME "netanalyzer")
    set(CPACK_SET_DESTDIR TRUE)
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

    # DEB -------------------------------------------------------------------
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "NetAnalyzer Project <netanalyzer@localhost>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "net")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    # Qt-библиотеки входят в пакет; здесь — системные зависимости,
    # необходимые для их работы (glibc, стек X11/xcb, ICU, шрифты).
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libc6 (>= 2.35), libstdc++6 (>= 11.3), libgcc-s1, \
libicu72 | libicu74 | libicu76, libdbus-1-3, libegl1, \
libfontconfig1, libfreetype6, libglib2.0-0 (>= 2.56), libx11-xcb1, \
libxcb-cursor0, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, \
libxcb-randr0, libxcb-render-util0, libxcb-shape0, libxcb-xinerama0, \
libxkbcommon0, libxkbcommon-x11-0")

    # RPM -------------------------------------------------------------------
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Internet")
    # Qt-библиотеки входят в RPM: авто-сканирование ELF-зависимостей добавило
    # бы требование дистрибутивного Qt (и даже его плагинов) — отключаем,
    # зависимости заданы явно ниже (CPACK_RPM_PACKAGE_REQUIRES).
    set(CPACK_RPM_PACKAGE_AUTOREQPROV 0)
    # Имена пакетов Fedora/openSUSE; Qt-библиотеки входят в состав RPM.
    set(CPACK_RPM_PACKAGE_REQUIRES
        "glibc, libstdc++, libgcc, libicu, dbus-libs, mesa-libEGL, \
fontconfig, freetype, glib2, libX11-xcb, libxcb, libxcb-cursor, \
libxkbcommon, libxkbcommon-x11")
endif()

include(CPack)
