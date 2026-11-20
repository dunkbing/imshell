# android: pack a shared library into a signed apk, or a play store bundle, no gradle (cmake/android/apk.sh)
#
#   imshell_android_apk(myapp PACKAGE com.example.myapp LABEL "My App"
#                      [ACTIVITY io.imshell.ImShellActivity] [JAVA extra.java...] [ASSETS dir] [ICON icon.png]
#                      [PERMISSIONS android.permission.INTERNET...] [VERSION 1.0] [VERSION_CODE 1])
#
# adds the target <lib>_apk, which writes <lib>.apk next to the library (debuggable in Debug), and <lib>_aab
# (not built by default), which writes <lib>.aab for google play. both sign with the key in the environment
# (IMSHELL_ANDROID_KEYSTORE, _KEY_ALIAS, _KEYSTORE_PASS, _KEY_PASS) when set; the apk falls back to the debug key.
# the sdk is the ndk's parent (ndk/<version> inside it); needs a jdk (javac, jar, keytool, jarsigner) on PATH, and
# bundletool for the aab
function(imshell_android_apk lib)
    set(dir ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/android)
    cmake_parse_arguments(APK "" "PACKAGE;LABEL;ACTIVITY;ASSETS;ICON;VERSION;VERSION_CODE"
                          "JAVA;PERMISSIONS" ${ARGN})
    if(NOT APK_ACTIVITY)
        set(APK_ACTIVITY io.imshell.ImShellActivity)
    endif()
    if(NOT APK_VERSION)
        set(APK_VERSION 1.0)
    endif()
    if(NOT APK_VERSION_CODE)
        set(APK_VERSION_CODE 1)
    endif()
    if(NOT APK_ASSETS)
        set(APK_ASSETS -)
    endif()
    set(APK_ICON_ATTR "")
    if(APK_ICON)
        set(APK_ICON_ATTR "android:icon=\"@mipmap/ic_launcher\"")
    endif()
    set(APK_MIN_SDK 28)
    set(APK_LIB ${lib})
    set(APK_PERMISSIONS_XML "")
    foreach(p ${APK_PERMISSIONS})
        string(APPEND APK_PERMISSIONS_XML "    <uses-permission android:name=\"${p}\" />\n")
    endforeach()
    # google play wants native code that runs on 16 KB page devices (android 15+)
    target_link_options(${lib} PRIVATE "-Wl,-z,max-page-size=16384")
    set(work ${CMAKE_CURRENT_BINARY_DIR}/${lib}_apk)
    configure_file(${dir}/AndroidManifest.xml.in ${work}/AndroidManifest.xml @ONLY)
    set(ndk ${CMAKE_ANDROID_NDK})
    if(NOT ndk)
        set(ndk ${ANDROID_NDK})
    endif()
    get_filename_component(sdk ${ndk}/../.. ABSOLUTE)
    set(java ${dir}/../../android/io/imshell/ImShellActivity.java ${APK_JAVA})
    set(env IMSHELL_APK_DEBUG=$<CONFIG:Debug> IMSHELL_APK_ICON=${APK_ICON})
    add_custom_target(${lib}_apk ALL
        COMMAND ${CMAKE_COMMAND} -E env ${env} ${dir}/apk.sh ${sdk} ${work} ${work}/AndroidManifest.xml
                $<TARGET_FILE:${lib}> ${CMAKE_CURRENT_BINARY_DIR}/${lib}.apk ${APK_ASSETS} ${java}
        DEPENDS ${lib} ${java}
        VERBATIM)
    add_custom_target(${lib}_aab
        COMMAND ${CMAKE_COMMAND} -E env ${env} IMSHELL_APK_FORMAT=aab ${dir}/apk.sh ${sdk} ${work}_aab
                ${work}/AndroidManifest.xml $<TARGET_FILE:${lib}> ${CMAKE_CURRENT_BINARY_DIR}/${lib}.aab ${APK_ASSETS} ${java}
        DEPENDS ${lib} ${java}
        VERBATIM)
endfunction()
