# WinUI 3 (Windows App SDK) for a Windows host that draws native views with it, without
# msbuild: the nuget packages are plain zip files fetched from nuget.org into the build tree,
# and C++/WinRT's projection headers are generated from their metadata.
#
#   include(cmake/ImShellWinUI.cmake)
#   imshell_winui(mytarget)          # its own WinUI code: the projection headers, the bootstrap library
#   imshell_winui_runtime(myexe)     # an executable: the bootstrap dll next to it
#
# imshell itself uses this with -DIMSHELL_WINUI=ON (native controls, imshell/winui.hpp); a host that
# also writes WinUI code of its own calls imshell_winui for its target too.
#
# at run time the Windows App Runtime of the same version must be installed on the machine (the
# host's installer ships the redistributable); without it MddBootstrapInitialize2 fails and the host
# falls back to something else. the bootstrap dll is delay loaded, so an executable that does not
# have it beside it falls back the same way instead of failing to start.

set(IMSHELL_WINAPPSDK_VERSION 2.7.0 CACHE STRING "Windows App SDK version (nuget package versions)")
set(IMSHELL_WEBVIEW2_VERSION 1.0.3719.77 CACHE STRING "Microsoft.Web.WebView2 version (its metadata, which WinUI's winmd refers to)")

# sha256 of the nupkg files of the pinned versions: a download that does not match is refused. another
# version has no pinned hash (a warning); add its hash here when bumping
set(_IMSHELL_SHA256_microsoft.windowsappsdk.foundation_2.7.0 4131660bda7476eef751ce134c9847b5a79e4051809bffbf9dd171f4246afdf8)
set(_IMSHELL_SHA256_microsoft.windowsappsdk.winui_2.7.0 027ecdc0c51a2d31fd25985616ef2e9438f02ee3e7b6e72fd0601ddb119688b4)
set(_IMSHELL_SHA256_microsoft.windowsappsdk.interactiveexperiences_2.7.0 5df51fd9e30a6093f737698ae8ad4e8d62d87f7081b00aa6a4ea0b893f03e1c2)
set(_IMSHELL_SHA256_microsoft.windowsappsdk.runtime_2.7.0 326b47f40f051e767bfc83ce99c753c758bbba75a25f0a4febab8cd9ea8b5152)
set(_IMSHELL_SHA256_microsoft.web.webview2_1.0.3719.77 2f6be3a10a1a8d6d1fde986af4131dab344f8181fffe75590824e0f4b037ed73)

function(_imshell_nuget id version out)
    string(TOLOWER ${id} lower)
    set(dir ${CMAKE_BINARY_DIR}/_nuget/${lower}.${version})
    if(NOT EXISTS ${dir}/.done)
        set(file ${CMAKE_BINARY_DIR}/_nuget/${lower}.${version}.nupkg)
        set(hash "${_IMSHELL_SHA256_${lower}_${version}}")
        if(hash)
            set(check EXPECTED_HASH SHA256=${hash})
        else()
            set(check "")
            message(WARNING "imshell: no pinned sha256 for nuget package ${id} ${version}")
        endif()
        file(DOWNLOAD https://api.nuget.org/v3-flatcontainer/${lower}/${version}/${lower}.${version}.nupkg
             ${file} STATUS status TLS_VERIFY ON ${check})
        list(GET status 0 code)
        if(code)
            file(REMOVE ${file})
            message(FATAL_ERROR "imshell: fetching nuget package ${id} ${version} failed or did not match its pinned hash: ${status}")
        endif()
        file(ARCHIVE_EXTRACT INPUT ${file} DESTINATION ${dir})
        file(WRITE ${dir}/.done "")
    endif()
    set(${out} ${dir} PARENT_SCOPE)
endfunction()

function(imshell_winui target)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "imshell_winui: only x64 is set up")
    endif()
    set(v ${IMSHELL_WINAPPSDK_VERSION})
    _imshell_nuget(Microsoft.WindowsAppSDK.Foundation ${v} foundation)
    _imshell_nuget(Microsoft.WindowsAppSDK.WinUI ${v} winui)
    _imshell_nuget(Microsoft.WindowsAppSDK.InteractiveExperiences ${v} experiences)
    _imshell_nuget(Microsoft.WindowsAppSDK.Runtime ${v} runtime)
    # the Windows SDK's own cppwinrt: the host's other sources (and imshell) compile against the SDK's
    # C++/WinRT headers, and a different version in the projection fails to link (detect_mismatch)
    find_program(IMSHELL_CPPWINRT cppwinrt.exe REQUIRED)
    _imshell_nuget(Microsoft.Web.WebView2 ${IMSHELL_WEBVIEW2_VERSION} webview2)

    # keyed by the versions: another version must not reuse these headers
    set(gen ${CMAKE_BINARY_DIR}/_winui_gen/${v}-${IMSHELL_WEBVIEW2_VERSION})
    if(NOT EXISTS ${gen}/winrt/Microsoft.UI.Xaml.Hosting.h)
        execute_process(
            COMMAND ${IMSHELL_CPPWINRT} -input sdk
                    -input ${foundation}/metadata -input ${winui}/metadata
                    -input ${experiences}/metadata/10.0.18362.0
                    -input ${webview2}/lib/Microsoft.Web.WebView2.Core.winmd
                    -output ${gen} -optimize
            RESULT_VARIABLE result OUTPUT_VARIABLE log ERROR_VARIABLE log)
        if(result)
            message(FATAL_ERROR "imshell: cppwinrt failed: ${log}")
        endif()
    endif()

    target_include_directories(${target} SYSTEM PRIVATE ${gen}
        ${foundation}/include ${winui}/include ${experiences}/include ${runtime}/include)
    # the full path: a static library's consumers link it too and have no link directory for it
    target_link_libraries(${target} PRIVATE ${foundation}/lib/native/x64/Microsoft.WindowsAppRuntime.Bootstrap.lib windowsapp)
    target_compile_definitions(${target} PRIVATE IMSHELL_WINUI=1)
    # the bootstrap dll is loaded on the first call, so a missing one fails that call (WinUIAvailable
    # checks first) and not the start of the executable. a static library hands this on to what links it
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "STATIC_LIBRARY")
        target_link_options(${target} INTERFACE /DELAYLOAD:Microsoft.WindowsAppRuntime.Bootstrap.dll)
        target_link_libraries(${target} INTERFACE delayimp)
    else()
        target_link_options(${target} PRIVATE /DELAYLOAD:Microsoft.WindowsAppRuntime.Bootstrap.dll)
        target_link_libraries(${target} PRIVATE delayimp)
    endif()
    set(IMSHELL_WINUI_BOOTSTRAP_DLL ${foundation}/runtimes/win-x64/native/Microsoft.WindowsAppRuntime.Bootstrap.dll
        CACHE INTERNAL "the bootstrap dll an executable using WinUI ships next to itself")
endfunction()

# an executable that links imshell built with IMSHELL_WINUI: the bootstrap dll next to it
function(imshell_winui_runtime target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IMSHELL_WINUI_BOOTSTRAP_DLL} $<TARGET_FILE_DIR:${target}>)
endfunction()
