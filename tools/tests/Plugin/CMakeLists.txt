find_package(Qt6 REQUIRED COMPONENTS
    Test
    Gui
    Widgets
)

function(add_plugin_fixture fixture_target fixture_id fixture_version fixture_deps)
    set(fixture_json "${CMAKE_CURRENT_BINARY_DIR}/${fixture_target}.json")
    set(fixture_deps_cpp "")
    set(fixture_deps_json "")
    foreach(dependency IN LISTS fixture_deps)
        if(fixture_deps_cpp)
            string(APPEND fixture_deps_cpp ", ")
            string(APPEND fixture_deps_json ", ")
        endif()
        string(APPEND fixture_deps_cpp "\"${dependency}\"")
        string(APPEND fixture_deps_json "\"${dependency}\"")
    endforeach()
    file(WRITE "${fixture_json}"
        "{\"api\":1,\"id\":\"${fixture_id}\",\"name\":\"${fixture_id}\","
        "\"version\":\"${fixture_version}\",\"dependencies\":[${fixture_deps_json}]}"
    )
    configure_file(Fixture.cpp.in "${fixture_target}.cpp" @ONLY)
    set(fixture_headers "")
    if(fixture_target STREQUAL "PluginFixtureProvider")
        list(APPEND fixture_headers PUBLIC_HEADERS inc/Termaster/PluginFixtureProvider/Api.h)
    endif()
    termaster_add_plugin(${fixture_target}
        OUTPUT_ROOT "${CMAKE_CURRENT_BINARY_DIR}/plugin"
        SOURCES
            "${CMAKE_CURRENT_BINARY_DIR}/${fixture_target}.cpp"
        ${fixture_headers}
    )
    set_target_properties(${fixture_target} PROPERTIES
        AUTOMOC ON
        CXX_STANDARD 20
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
    target_link_libraries(${fixture_target} PRIVATE Qt6::Core)
endfunction()

add_plugin_fixture(PluginFixtureProvider provider 1.0.0 "")
target_compile_definitions(PluginFixtureProvider PRIVATE FIXTURE_PROVIDER)
add_plugin_fixture(PluginFixtureConsumer consumer 1.0.0 provider)
target_compile_definitions(PluginFixtureConsumer PRIVATE FIXTURE_CONSUMER)
target_link_libraries(PluginFixtureConsumer PRIVATE PluginFixtureProvider)
add_plugin_fixture(PluginFixtureLeaf leaf 1.0.0 consumer)
add_plugin_fixture(PluginFixtureProviderV2 provider 2.0.0 "")
add_plugin_fixture(PluginFixtureMissing missing 1.0.0 absent)
add_plugin_fixture(PluginFixtureCycleA cycleA 1.0.0 cycleB)
add_plugin_fixture(PluginFixtureCycleB cycleB 1.0.0 cycleA)
add_plugin_fixture(PluginFixtureBadId "bad id" 1.0.0 "")
add_plugin_fixture(PluginFixtureStartFailure startFailure 1.0.0 provider)
target_compile_definitions(PluginFixtureStartFailure PRIVATE FIXTURE_START_FAILURE)
add_plugin_fixture(PluginFixtureStopFailure stopFailure 1.0.0 provider)
target_compile_definitions(PluginFixtureStopFailure PRIVATE FIXTURE_STOP_FAILURE)

add_plugin_fixture(PluginFixtureProcessor processor 1.0.0 "")
target_compile_definitions(PluginFixtureProcessor PRIVATE FIXTURE_PROCESSOR)

qt_add_executable(PluginIntegrationTests PluginIntegrationTests.cpp)
target_compile_features(PluginIntegrationTests PRIVATE cxx_std_20)
target_link_libraries(PluginIntegrationTests PRIVATE Core Qt6::Test yaml-cpp)
foreach(fixture IN ITEMS
    Provider
    Consumer
    Leaf
    ProviderV2
    Processor
    Missing
    CycleA
    CycleB
    BadId
    StartFailure
    StopFailure
)
    add_dependencies(PluginIntegrationTests PluginFixture${fixture})
    target_compile_definitions(PluginIntegrationTests PRIVATE
        "FIXTURE_${fixture}=\"$<TARGET_FILE:PluginFixture${fixture}>\""
    )
endforeach()
add_test(NAME PluginIntegrationTests COMMAND PluginIntegrationTests)
set_tests_properties(PluginIntegrationTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)

qt_add_executable(PluginWindowTests
    PluginWindowTests.cpp
)
target_compile_features(PluginWindowTests PRIVATE cxx_std_20)
target_link_libraries(PluginWindowTests PRIVATE
    Core
    Qt6::Test
)
foreach(fixture IN ITEMS
    Provider
    Consumer
    Leaf
    Missing
    StartFailure
)
    add_dependencies(PluginWindowTests PluginFixture${fixture})
    target_compile_definitions(PluginWindowTests PRIVATE
        "FIXTURE_${fixture}=\"$<TARGET_FILE:PluginFixture${fixture}>\""
    )
endforeach()
add_test(NAME PluginWindowTests COMMAND PluginWindowTests)
set_tests_properties(PluginWindowTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)
