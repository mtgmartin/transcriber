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
# transcriber_fetch_npm(<name> <version> <sha512 hex> <file inside the package> <out var>)
# Sets <out var> to the path of the extracted file.
set(TRANSCRIBER_WEB_LIBRARIES "")

function(transcriber_fetch_npm name version sha512 path_in_package out_var)
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

    set(${out_var} "${extracted}" PARENT_SCOPE)
endfunction()

transcriber_fetch_npm(verovio 6.3.0
    d69e9684fff0dd6ac084f5434cd5031fd5153c064e9c79d95535b172513c85a1dc71523fabc73ef7dbcb1faad180bf4cb9868547d1c5e14243e8cc96d827ccc2
    dist/verovio-toolkit-wasm.js
    verovio_js)

transcriber_fetch_npm(jspdf 4.2.1
    632017caf9e68d36d1e1b1d044bcdec770ae20d0839509c1aa8498ca32704cfdb1f630cbb8a0f3cbb68a525d21831dee85c97bc73837d9a827e6f9627ba1c895
    dist/jspdf.umd.min.js
    jspdf_js)

transcriber_fetch_npm(svg2pdf.js 2.8.1
    0335df3c78c79c5b7d7697513e159604e2a2a673125906512a25771534cf758eb0c11813de569b6c0e2cd072106ac90d870021adc236bb6ac3843f23f2e75f35
    dist/svg2pdf.umd.min.js
    svg2pdf_js)

# DejaVu Serif (Bitstream Vera licence, free to embed): the font of titles and text on the page and in the PDF.
# It has the Latin Extended letters (c with caron and the like) that jsPDF's standard fonts lack. Three styles are
# enough: text in notation is regular, bold (tempo words) or italic (expression text).
foreach(style IN ITEMS "" "-Bold" "-Italic")
    string(REPLACE "-" "_" style_var "DejaVuSerif${style}")
    transcriber_fetch_npm(dejavu-fonts-ttf 2.37.3
        7f585dee325b7906b5556730f8ad8aad35d2e74cd331a1e9542e1720a26935c0de611e5a8ccb63fe22e540360dbcb38a6a6501dc04555427fbf65370355ced49
        ttf/DejaVuSerif${style}.ttf
        ${style_var})
endforeach()

# Verovio starts its WASM runtime asynchronously
 and only calls onRuntimeInitialized if a
# handler is already attached; there is no "already started" flag. A handler attached by a
# later <script> tag can therefore miss the call (seen in Live: 1 of 6 page loads). Code
# appended to the same file runs before any async continuation, so the race cannot happen.
set(verovio_patched "${TRANSCRIBER_DEPS_DIR}/web/verovio-toolkit-wasm.js")
file(MAKE_DIRECTORY "${TRANSCRIBER_DEPS_DIR}/web")
file(COPY_FILE "${verovio_js}" "${verovio_patched}")
file(APPEND "${verovio_patched}"
    "\n;/* Added by Transcriber's build (cmake/Dependencies.cmake) */\n"
    "window.verovioReady = new Promise(function (resolve) { verovio.module.onRuntimeInitialized = resolve; });\n")

set(TRANSCRIBER_WEB_LIBRARIES "${verovio_patched}" "${jspdf_js}" "${svg2pdf_js}" "${DejaVuSerif}" "${DejaVuSerif_Bold}" "${DejaVuSerif_Italic}")
