# Pinned third-party downloads. Every file is checked against the hash published by
# its registry (nuget.org / registry.npmjs.org), so a changed download fails the build.

set(TRANSCRIBER_DEPS_DIR "${CMAKE_BINARY_DIR}/deps")

# --- WebView2 SDK (static loader), found by JUCE's FindWebView2.cmake --------------
set(WEBVIEW2_VERSION 1.0.3485.44)   # the version JUCE 9.0.3's FindWebView2 suggests
# Not named WEBVIEW2_ROOT: CMake treats <Package>_ROOT variables as find_package hints.
set(TRANSCRIBER_WEBVIEW2_DIR "${TRANSCRIBER_DEPS_DIR}/nuget/Microsoft.Web.WebView2.${WEBVIEW2_VERSION}")

if(NOT EXISTS "${TRANSCRIBER_WEBVIEW2_DIR}/build/native/include/WebView2.h")
    set(nupkg "${TRANSCRIBER_DEPS_DIR}/webview2.${WEBVIEW2_VERSION}.nupkg")
    file(DOWNLOAD
        "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${WEBVIEW2_VERSION}/microsoft.web.webview2.${WEBVIEW2_VERSION}.nupkg"
        "${nupkg}"
        EXPECTED_HASH SHA256=BC09150B179246AC90189649B13BE8E6B11B3AC200E817E18DF106E1F3CF489E
        TLS_VERIFY ON)
    file(ARCHIVE_EXTRACT INPUT "${nupkg}" DESTINATION "${TRANSCRIBER_WEBVIEW2_DIR}")
endif()

set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${TRANSCRIBER_DEPS_DIR}/nuget")

# --- Web UI libraries from npm ------------------------------------------------------
# transcriber_fetch_npm(<name> <version> <sha512 hex> <file inside the package>)
# Appends the extracted file to TRANSCRIBER_WEB_LIBRARIES.
set(TRANSCRIBER_WEB_LIBRARIES "")

function(transcriber_fetch_npm name version sha512 path_in_package)
    get_filename_component(file_name "${path_in_package}" NAME)
    set(dest_dir "${TRANSCRIBER_DEPS_DIR}/npm/${name}-${version}")
    set(extracted "${dest_dir}/package/${path_in_package}")

    if(NOT EXISTS "${extracted}")
        set(tgz "${TRANSCRIBER_DEPS_DIR}/npm/${name}-${version}.tgz")
        file(DOWNLOAD
            "https://registry.npmjs.org/${name}/-/${name}-${version}.tgz"
            "${tgz}"
            EXPECTED_HASH SHA512=${sha512}
            TLS_VERIFY ON)
        file(ARCHIVE_EXTRACT INPUT "${tgz}" DESTINATION "${dest_dir}" PATTERNS "package/${path_in_package}")
    endif()

    if(NOT EXISTS "${extracted}")
        message(FATAL_ERROR "${path_in_package} was not found in ${name}-${version}.tgz")
    endif()

    set(TRANSCRIBER_WEB_LIBRARIES ${TRANSCRIBER_WEB_LIBRARIES} "${extracted}" PARENT_SCOPE)
endfunction()

transcriber_fetch_npm(verovio 6.3.0
    d69e9684fff0dd6ac084f5434cd5031fd5153c064e9c79d95535b172513c85a1dc71523fabc73ef7dbcb1faad180bf4cb9868547d1c5e14243e8cc96d827ccc2
    dist/verovio-toolkit-wasm.js)

transcriber_fetch_npm(jspdf 4.2.1
    632017caf9e68d36d1e1b1d044bcdec770ae20d0839509c1aa8498ca32704cfdb1f630cbb8a0f3cbb68a525d21831dee85c97bc73837d9a827e6f9627ba1c895
    dist/jspdf.umd.min.js)

transcriber_fetch_npm(svg2pdf.js 2.8.1
    0335df3c78c79c5b7d7697513e159604e2a2a673125906512a25771534cf758eb0c11813de569b6c0e2cd072106ac90d870021adc236bb6ac3843f23f2e75f35
    dist/svg2pdf.umd.min.js)
