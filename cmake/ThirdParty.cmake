# Reviewed offline notices are source assets; builds do not inspect installed kits.
function(termaster_add_third_party_resources target)
    set(bundle_source "${CMAKE_SOURCE_DIR}/src/App/Res/ThirdParty")
    file(GLOB_RECURSE bundle_resources CONFIGURE_DEPENDS
        "${bundle_source}/*"
    )
    qt_add_resources(${target} "thirdPartyNotices"
        PREFIX "/Termaster/App/ThirdParty"
        BASE "${bundle_source}"
        FILES
            ${bundle_resources}
    )

    add_custom_target(termasterThirdPartyBundle ALL
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${bundle_source}"
            "$<TARGET_FILE_DIR:Termaster>/third-party"
        COMMENT "Copying offline third-party notices"
        VERBATIM
    )
    add_dependencies(Termaster termasterThirdPartyBundle)
endfunction()
