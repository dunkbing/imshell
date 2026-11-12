# cmake -P, from imshell_app()'s <target>-app: the host's exe as <NAME>.app with sparkle in it,
# signed, zipped, and notarized when the environment has a notary's credentials.
# environment: IMSHELL_SIGN_IDENTITY, a "Developer ID Application: ..." keychain identity (unset:
# ad hoc); IMSHELL_NOTARY_PROFILE, a notarytool keychain profile, or IMSHELL_NOTARY_KEY (an app
# store connect api key, the .p8 file), IMSHELL_NOTARY_KEY_ID and IMSHELL_NOTARY_ISSUER
function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc)
    if(rc)
        message(FATAL_ERROR "failed (${rc}): ${ARGN}")
    endif()
endfunction()

set(app "${OUT}/${NAME}.app")
get_filename_component(exeName "${EXE}" NAME)
set(bin "${app}/Contents/MacOS/${exeName}")
file(REMOVE_RECURSE "${app}")
file(MAKE_DIRECTORY "${app}/Contents/MacOS")
file(COPY_FILE "${EXE}" "${bin}")
run(strip -x "${bin}")

# the build tree's rpaths (sparkle's download among them) out, the bundle's Frameworks in
execute_process(COMMAND otool -l "${bin}" OUTPUT_VARIABLE load)
string(REGEX MATCHALL "LC_RPATH\n[^\n]*\n +path [^\n]+ \\(offset" rpaths "${load}")
foreach(entry IN LISTS rpaths)
    string(REGEX REPLACE ".* path (.+) \\(offset$" "\\1" path "${entry}")
    run(install_name_tool -delete_rpath "${path}" "${bin}")
endforeach()
run(install_name_tool -add_rpath "@executable_path/../Frameworks" "${bin}")

set(updates "")
if(SPARKLE AND APPCAST AND PUBLIC_KEY)
    set(updates "
    <key>SUFeedURL</key><string>${APPCAST}</string>
    <key>SUPublicEDKey</key><string>${PUBLIC_KEY}</string>
    <key>SUEnableAutomaticChecks</key><true/>")
endif()
set(minOs "")
if(MIN_OS)
    set(minOs "\n    <key>LSMinimumSystemVersion</key><string>${MIN_OS}</string>")
endif()
file(WRITE "${app}/Contents/Info.plist" "<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
    <key>CFBundleName</key><string>${NAME}</string>
    <key>CFBundleDisplayName</key><string>${NAME}</string>
    <key>CFBundleIdentifier</key><string>${ID}</string>
    <key>CFBundleExecutable</key><string>${exeName}</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>${VERSION}</string>
    <key>CFBundleVersion</key><string>${VERSION}</string>${minOs}
    <key>NSHighResolutionCapable</key><true/>${updates}
</dict>
</plist>
")

# sparkle's parts are signed inside out, as its docs ask, then the app
set(identity "$ENV{IMSHELL_SIGN_IDENTITY}")
set(sign codesign --force -s -)
if(identity)
    set(sign codesign --force --timestamp --options runtime -s "${identity}")
endif()
if(SPARKLE)
    set(fw "${app}/Contents/Frameworks/Sparkle.framework")
    file(MAKE_DIRECTORY "${app}/Contents/Frameworks")
    run(ditto "${SPARKLE}" "${fw}") # keeps the framework's symlinks
    run(${sign} "${fw}/Versions/B/XPCServices/Installer.xpc")
    run(${sign} --preserve-metadata=entitlements "${fw}/Versions/B/XPCServices/Downloader.xpc")
    run(${sign} "${fw}/Versions/B/Autoupdate")
    run(${sign} "${fw}/Versions/B/Updater.app")
    run(${sign} "${fw}")
endif()
run(${sign} "${app}")

file(REMOVE "${ZIP}")
run(ditto -c -k --keepParent "${app}" "${ZIP}")
if(DEFINED ENV{IMSHELL_NOTARY_PROFILE})
    set(notary --keychain-profile "$ENV{IMSHELL_NOTARY_PROFILE}")
elseif(DEFINED ENV{IMSHELL_NOTARY_KEY})
    set(notary --key "$ENV{IMSHELL_NOTARY_KEY}" --key-id "$ENV{IMSHELL_NOTARY_KEY_ID}" --issuer "$ENV{IMSHELL_NOTARY_ISSUER}")
endif()
if(notary)
    run(xcrun notarytool submit "${ZIP}" ${notary} --wait)
    run(xcrun stapler staple "${app}") # fails unless apple accepted it
    file(REMOVE "${ZIP}")
    run(ditto -c -k --keepParent "${app}" "${ZIP}")
endif()
message(STATUS "${ZIP} (${VERSION})")
