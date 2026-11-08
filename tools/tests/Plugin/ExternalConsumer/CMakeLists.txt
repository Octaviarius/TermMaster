cmake_minimum_required(VERSION 3.21)

project(ExternalPluginConsumer LANGUAGES CXX)

find_package(TermasterPluginFixtureProvider CONFIG REQUIRED)

termaster_add_plugin(ExternalConsumer
    SOURCES
        ExternalConsumer.cpp
    DEPENDENCIES
        Termaster::PluginFixtureProvider
)
