# android: pack a shared library into a signed debug apk, no gradle (cmake/android/apk.sh)
#
#   mydear_android_apk(myapp PACKAGE com.example.myapp LABEL "My App"
#                      [ACTIVITY io.mydear.MydearActivity] [JAVA extra.java...] [ASSETS dir]
#                      [PERMISSIONS android.permission.INTERNET...] [VERSION 1.0] [VERSION_CODE 1])
#
# adds the target <lib>_apk, which writes <lib>.apk next to the library. the sdk is the ndk's
# parent (ndk/<version> inside it); needs a jdk (javac, jar, keytool) on PATH

function(mydear_android_apk lib)
    set(dir ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/android)
    cmake_parse_arguments(APK "" "PACKAGE;LABEL;ACTIVITY;ASSETS;VERSION;VERSION_CODE"
                          "JAVA;PERMISSIONS" ${ARGN})
    if(NOT APK_ACTIVITY)
        set(APK_ACTIVITY io.mydear.MydearActivity)
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
    set(APK_MIN_SDK 28)
    set(APK_LIB ${lib})
    set(APK_PERMISSIONS_XML "")
    foreach(p ${APK_PERMISSIONS})
        string(APPEND APK_PERMISSIONS_XML "    <uses-permission android:name=\"${p}\" />\n")
    endforeach()
    set(work ${CMAKE_CURRENT_BINARY_DIR}/${lib}_apk)
    configure_file(${dir}/AndroidManifest.xml.in ${work}/AndroidManifest.xml @ONLY)
    set(ndk ${CMAKE_ANDROID_NDK})
    if(NOT ndk)
        set(ndk ${ANDROID_NDK})
    endif()
    get_filename_component(sdk ${ndk}/../.. ABSOLUTE)
    set(java ${dir}/../../android/io/mydear/MydearActivity.java ${APK_JAVA})
    add_custom_target(${lib}_apk ALL
        COMMAND ${dir}/apk.sh ${sdk} ${work} ${work}/AndroidManifest.xml
                $<TARGET_FILE:${lib}> ${CMAKE_CURRENT_BINARY_DIR}/${lib}.apk ${APK_ASSETS} ${java}
        DEPENDS ${lib} ${java}
        VERBATIM)
endfunction()
