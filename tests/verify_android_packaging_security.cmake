if(NOT DEFINED BUDO_EXECUTABLE OR NOT DEFINED STAGING_ROOT OR NOT DEFINED BUILD_CACHE OR
   NOT DEFINED PYTHON_EXECUTABLE OR NOT DEFINED BUDO_SOURCE_DIR)
    message(FATAL_ERROR "BUDO_EXECUTABLE, STAGING_ROOT, BUILD_CACHE, PYTHON_EXECUTABLE, and BUDO_SOURCE_DIR are required")
endif()

# The packager keeps one build directory per app in a cache (the user cache by
# default). Point it at a test-owned directory; clear_android_build_cache()
# before a run makes staged_android_app_root() find the one it produced.
set(ENV{BUDO_ANDROID_BUILD_CACHE_DIR} "${BUILD_CACHE}")
get_filename_component(android_build_cache_parent "${BUILD_CACHE}" DIRECTORY)
file(MAKE_DIRECTORY "${android_build_cache_parent}")

function(clear_android_build_cache)
    file(REMOVE_RECURSE "${BUILD_CACHE}")
endfunction()

function(staged_android_app_root out)
    file(GLOB entries LIST_DIRECTORIES true RELATIVE "${BUILD_CACHE}" "${BUILD_CACHE}/*")
    set(found "")
    foreach(entry IN LISTS entries)
        if(IS_DIRECTORY "${BUILD_CACHE}/${entry}" AND NOT entry MATCHES "^\\.")
            list(APPEND found "${BUILD_CACHE}/${entry}")
        endif()
    endforeach()
    list(LENGTH found count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "Expected one Android build directory in ${BUILD_CACHE}, found: ${found}")
    endif()
    set(${out} "${found}" PARENT_SCOPE)
endfunction()

set(fixture_root "${STAGING_ROOT}-security-fixtures")
file(REMOVE_RECURSE "${fixture_root}")
file(MAKE_DIRECTORY "${fixture_root}")

function(write_managed_fixture name app_json)
    set(dir "${fixture_root}/${name}")
    file(MAKE_DIRECTORY "${dir}")
    file(WRITE "${dir}/main.js" "console.log('security fixture');\n")
    file(WRITE "${dir}/app.json" "${app_json}")
endfunction()

function(expect_staging_failure name expected)
    execute_process(
        COMMAND "${BUDO_EXECUTABLE}" android-apk "${fixture_root}/${name}" --no-build
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(result EQUAL 0)
        message(FATAL_ERROR "Android security fixture unexpectedly succeeded: ${name}")
    endif()
    set(combined "${output}\n${error}")
    string(FIND "${combined}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR
            "Android security fixture ${name} did not report '${expected}':\n${combined}")
    endif()
endfunction()

write_managed_fixture(valid_escaping [=[{
  "package_name": "dev.budo.securityfixture",
  "name": "Rock & <Roll> > \"quoted\"",
  "author": "A & B <C> > \"D\" 'E'",
  "version_name": "1.2\"$tag\\end",
  "version_code": 42,
  "orientation": "sensorLandscape",
  "permissions": ["INTERNET", "POST_NOTIFICATIONS"],
  "min_sdk": 26,
  "target_sdk": 35
}
]=])
clear_android_build_cache()
execute_process(
    COMMAND "${BUDO_EXECUTABLE}" android-apk "${fixture_root}/valid_escaping" --no-build
    RESULT_VARIABLE valid_result
    OUTPUT_VARIABLE valid_output
    ERROR_VARIABLE valid_error)
if(NOT valid_result EQUAL 0)
    message(FATAL_ERROR "Escaped Android metadata staging failed:\n${valid_output}\n${valid_error}")
endif()

staged_android_app_root(app_root)
set(strings_path "${app_root}/android/app/src/main/res/values/strings.xml")
set(manifest_path "${app_root}/android/app/src/main/AndroidManifest.xml")
set(gradle_path "${app_root}/android/app/build.gradle")
file(READ "${strings_path}" strings)
file(READ "${manifest_path}" manifest)
file(READ "${gradle_path}" gradle)
foreach(check IN ITEMS
        [=[Rock &amp; &lt;Roll&gt; &gt; "quoted"]=])
    string(FIND "${strings}" "${check}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Escaped strings.xml value is missing: ${check}")
    endif()
endforeach()
foreach(check IN ITEMS
        [=[android:value="A &amp; B &lt;C&gt; &gt; &quot;D&quot; &apos;E&apos;"]=]
        [=[android:screenOrientation="sensorLandscape"]=]
        [=[android.permission.POST_NOTIFICATIONS]=])
    string(FIND "${manifest}" "${check}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Escaped AndroidManifest.xml value is missing: ${check}")
    endif()
endforeach()
string(FIND "${gradle}" [=[versionName = "1.2\"\$tag\\end"]=] found)
if(found EQUAL -1)
    message(FATAL_ERROR "Escaped Gradle versionName is missing")
endif()

write_managed_fixture(inferred_package [=[{
  "name": "Terrain Physics"
}
]=])
clear_android_build_cache()
execute_process(
    COMMAND "${BUDO_EXECUTABLE}" android-apk "${fixture_root}/inferred_package/" --no-build
    RESULT_VARIABLE inferred_package_result
    OUTPUT_VARIABLE inferred_package_output
    ERROR_VARIABLE inferred_package_error)
if(NOT inferred_package_result EQUAL 0)
    message(FATAL_ERROR
        "Display-name package inference failed:\n${inferred_package_output}\n${inferred_package_error}")
endif()
staged_android_app_root(app_root)
file(READ "${app_root}/android/app/build.gradle" inferred_package_gradle)
foreach(check IN ITEMS
        [=[namespace = 'com.budo.terrain_physics']=]
        [=[applicationId = "com.budo.terrain_physics"]=])
    string(FIND "${inferred_package_gradle}" "${check}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Display-name package inference is missing: ${check}")
    endif()
endforeach()

set(folder_fallback_dir "${fixture_root}/folder_fallback")
file(MAKE_DIRECTORY "${folder_fallback_dir}")
file(WRITE "${folder_fallback_dir}/main.js" "console.log('folder fallback');\n")
clear_android_build_cache()
execute_process(
    COMMAND "${BUDO_EXECUTABLE}" android-apk "${folder_fallback_dir}/" --no-build
    RESULT_VARIABLE folder_fallback_result
    OUTPUT_VARIABLE folder_fallback_output
    ERROR_VARIABLE folder_fallback_error)
if(NOT folder_fallback_result EQUAL 0)
    message(FATAL_ERROR
        "Trailing-slash folder package inference failed:\n${folder_fallback_output}\n${folder_fallback_error}")
endif()
staged_android_app_root(app_root)
file(READ "${app_root}/android/app/build.gradle" folder_fallback_gradle)
foreach(check IN ITEMS
        [=[namespace = 'com.budo.folder_fallback']=]
        [=[applicationId = "com.budo.folder_fallback"]=])
    string(FIND "${folder_fallback_gradle}" "${check}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Trailing-slash folder package inference is missing: ${check}")
    endif()
endforeach()

write_managed_fixture(package_ignore [=[{
  "name":"Package ignore",
  "package_ignore":["dev-notes.txt","source/maps"]
}]=])
file(WRITE "${fixture_root}/package_ignore/dev-notes.txt" "ignored file\n")
file(WRITE "${fixture_root}/package_ignore/dev-notes.txt.backup" "kept file\n")
file(MAKE_DIRECTORY "${fixture_root}/package_ignore/source/maps")
file(WRITE "${fixture_root}/package_ignore/source/maps/level.json" "ignored directory asset\n")
file(MAKE_DIRECTORY "${fixture_root}/package_ignore/source/maps-old")
file(WRITE "${fixture_root}/package_ignore/source/maps-old/level.json" "kept directory asset\n")
clear_android_build_cache()
execute_process(
    COMMAND "${BUDO_EXECUTABLE}" android-apk "${fixture_root}/package_ignore" --no-build
    RESULT_VARIABLE package_ignore_result
    OUTPUT_VARIABLE package_ignore_output
    ERROR_VARIABLE package_ignore_error)
if(NOT package_ignore_result EQUAL 0)
    message(FATAL_ERROR
        "package_ignore staging failed:\n${package_ignore_output}\n${package_ignore_error}")
endif()
staged_android_app_root(app_root)
set(staged_assets "${app_root}/android/app/src/main/assets/app")
if(EXISTS "${staged_assets}/dev-notes.txt" OR EXISTS "${staged_assets}/source/maps")
    message(FATAL_ERROR "package_ignore entries were staged")
endif()
if(NOT EXISTS "${staged_assets}/dev-notes.txt.backup" OR
   NOT EXISTS "${staged_assets}/source/maps-old/level.json")
    message(FATAL_ERROR "package_ignore matched a non-identical path prefix")
endif()

write_managed_fixture(package_ignore_traversal [=[{
  "name":"Invalid package ignore",
  "package_ignore":["source/../secret.txt"]
}]=])
expect_staging_failure(package_ignore_traversal "package_ignore entry contains an invalid or traversal component")

write_managed_fixture(escaping_symlink [=[{"name":"Escaping symlink"}]=])
file(WRITE "${fixture_root}/outside-secret.txt" "must not be staged\n")
file(CREATE_LINK "${fixture_root}/outside-secret.txt"
                 "${fixture_root}/escaping_symlink/leak.txt" SYMBOLIC RESULT link_result)
if(NOT link_result STREQUAL "0")
    message(FATAL_ERROR "Could not create escaping symlink fixture: ${link_result}")
endif()
expect_staging_failure(escaping_symlink "must not contain symlinks")

write_managed_fixture(in_root_symlink [=[{"name":"In-root symlink"}]=])
file(WRITE "${fixture_root}/in_root_symlink/data.txt" "ordinary asset\n")
file(CREATE_LINK "data.txt" "${fixture_root}/in_root_symlink/alias.txt"
                 SYMBOLIC RESULT link_result)
if(NOT link_result STREQUAL "0")
    message(FATAL_ERROR "Could not create in-root symlink fixture: ${link_result}")
endif()
expect_staging_failure(in_root_symlink "must not contain symlinks")

write_managed_fixture(icon_traversal [=[{"name":"Icon traversal","icon":"../outside-secret.txt"}]=])
expect_staging_failure(icon_traversal "traversal component")

write_managed_fixture(icon_absolute [=[{"name":"Absolute icon","icon":"/etc/hosts"}]=])
expect_staging_failure(icon_absolute "project-relative path")

# icon.png is excluded from managed assets, so this specifically proves that
# the exclusion cannot hide a symlink from icon rendering validation.
write_managed_fixture(excluded_icon_symlink [=[{"name":"Excluded icon symlink","icon":"icon.png"}]=])
file(CREATE_LINK "../outside-secret.txt"
                 "${fixture_root}/excluded_icon_symlink/icon.png" SYMBOLIC RESULT link_result)
if(NOT link_result STREQUAL "0")
    message(FATAL_ERROR "Could not create excluded icon.png symlink fixture: ${link_result}")
endif()
expect_staging_failure(excluded_icon_symlink "without symlinks")

write_managed_fixture(invalid_orientation [=[{"name":"Invalid orientation","orientation":"sideways"}]=])
expect_staging_failure(invalid_orientation "Invalid Android orientation")

write_managed_fixture(invalid_permission [=[{"name":"Invalid permission","permissions":["INTERNET\" /><injected"]}]=])
expect_staging_failure(invalid_permission "Invalid Android permission identifier")

write_managed_fixture(invalid_version_name [=[{"name":"Invalid version","version_name":"bad\ninjected"}]=])
expect_staging_failure(invalid_version_name "version_name must be non-empty and contain no control characters")

write_managed_fixture(conflicting_versions [=[{"name":"Conflicting version","version":"1.0","version_name":"2.0"}]=])
expect_staging_failure(conflicting_versions "Conflicting app.json version and version_name values")

write_managed_fixture(ambiguous_entrypoints [=[{"name":"Ambiguous entrypoints"}]=])
file(WRITE "${fixture_root}/ambiguous_entrypoints/main.lua" "print('ambiguous')\n")
expect_staging_failure(ambiguous_entrypoints "Ambiguous managed entrypoints")

write_managed_fixture(invalid_version_code [=[{"name":"Invalid version code","version_code":0}]=])
expect_staging_failure(invalid_version_code "version_code must be between")

write_managed_fixture(invalid_sdk_range [=[{"name":"Invalid SDK","min_sdk":36,"target_sdk":35}]=])
expect_staging_failure(invalid_sdk_range "SDK versions must satisfy")

write_managed_fixture(newline_injection [=[{"name":"Line\nInjected"}]=])
expect_staging_failure(newline_injection "name must be non-empty and contain no control characters")

write_managed_fixture(locale_traversal [=[{
    "name":"Locale traversal",
    "store_listing":{"short_description":"fixture","default_language":"../../escaped"}
}]=])
expect_staging_failure(locale_traversal "store_listing.default_language must be a locale")
if(EXISTS "${fixture_root}/escaped" OR EXISTS "${STAGING_ROOT}/escaped" OR
   EXISTS "${BUILD_CACHE}/escaped")
        message(FATAL_ERROR "Locale traversal fixture escaped the store listing directory")
endif()

write_managed_fixture(support_archive [=[{"name":"Support archive security"}]=])
set(traversal_archive "${fixture_root}/traversal-support.tar.gz")
execute_process(
    COMMAND "${PYTHON_EXECUTABLE}" "${BUDO_SOURCE_DIR}/tests/create_traversal_tar.py"
            "${traversal_archive}"
    RESULT_VARIABLE archive_result ERROR_VARIABLE archive_error)
if(NOT archive_result EQUAL 0)
    message(FATAL_ERROR "Could not create traversal archive: ${archive_error}")
endif()
file(SHA256 "${traversal_archive}" traversal_sha)

function(expect_support_failure name digest expected)
    set(test_tmp "${fixture_root}/support-cache-${name}")
    file(MAKE_DIRECTORY "${test_tmp}")
    file(CHMOD "${test_tmp}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "BUDO_ANDROID_SUPPORT_CACHE_DIR=${test_tmp}"
            "BUDO_ANDROID_SUPPORT_TEST_FORCE_PROVISION=1"
            "BUDO_ANDROID_SUPPORT_URL=file://${traversal_archive}"
            "BUDO_UNSAFE_ANDROID_SUPPORT_SHA256_OVERRIDE=1"
            "BUDO_ANDROID_SUPPORT_SHA256=${digest}"
            "${BUDO_EXECUTABLE}" android-apk "${fixture_root}/support_archive" --no-build
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(result EQUAL 0)
        message(FATAL_ERROR "Android support security fixture unexpectedly succeeded: ${name}")
    endif()
    string(FIND "${output}\n${error}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR
            "Android support fixture ${name} did not report '${expected}':\n${output}\n${error}")
    endif()
endfunction()

expect_support_failure(traversal "${traversal_sha}" "unsafe path")
expect_support_failure(corrupt_hash
    "0000000000000000000000000000000000000000000000000000000000000000"
    "checksum mismatch")

set(unsafe_cache "${fixture_root}/support-cache-unsafe-mode")
file(MAKE_DIRECTORY "${unsafe_cache}")
file(CHMOD "${unsafe_cache}" PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_WRITE GROUP_EXECUTE
    WORLD_READ WORLD_WRITE WORLD_EXECUTE)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "BUDO_ANDROID_SUPPORT_CACHE_DIR=${unsafe_cache}"
        "BUDO_ANDROID_SUPPORT_TEST_FORCE_PROVISION=1"
        "BUDO_ANDROID_SUPPORT_URL=file://${traversal_archive}"
        "BUDO_UNSAFE_ANDROID_SUPPORT_SHA256_OVERRIDE=1"
        "BUDO_ANDROID_SUPPORT_SHA256=${traversal_sha}"
        "${BUDO_EXECUTABLE}" android-apk "${fixture_root}/support_archive" --no-build
    RESULT_VARIABLE unsafe_result OUTPUT_VARIABLE unsafe_output ERROR_VARIABLE unsafe_error)
if(unsafe_result EQUAL 0 OR
   NOT "${unsafe_output}\n${unsafe_error}" MATCHES "unsafe permissions")
    message(FATAL_ERROR "Unsafe support cache mode was not rejected:\n${unsafe_output}\n${unsafe_error}")
endif()

set(valid_tree "${fixture_root}/valid-support")
foreach(path IN ITEMS
        "third_party/skia-android/out/android-arm64/libskia.a"
        "build/_deps/quickjs_ng-src/quickjs.c"
        "build/_deps/lua-src/src/lapi.c"
        "build/_deps/miniaudio-src/miniaudio.h"
        "build/_deps/miniaudio-src/extras/stb_vorbis.c"
        "third_party/stb/stb_image.h")
    get_filename_component(parent "${valid_tree}/${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    file(WRITE "${valid_tree}/${path}" "verified archive content\n")
endforeach()
file(WRITE "${valid_tree}/source-marker.txt" "from verified archive\n")
set(valid_archive "${fixture_root}/valid-support.tar.gz")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cfz "${valid_archive}" --format=gnutar .
    WORKING_DIRECTORY "${valid_tree}"
    RESULT_VARIABLE valid_archive_result ERROR_VARIABLE valid_archive_error)
if(NOT valid_archive_result EQUAL 0)
    message(FATAL_ERROR "Could not create valid support archive: ${valid_archive_error}")
endif()
file(SHA256 "${valid_archive}" valid_sha)

set(poison_cache "${fixture_root}/support-cache-poison")
set(poison_root "${poison_cache}/support-${valid_sha}")
file(MAKE_DIRECTORY "${poison_root}")
file(CHMOD "${poison_cache}" "${poison_root}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
file(WRITE "${poison_root}/.budo-android-support.sha256" "${valid_sha}\n")
file(WRITE "${poison_root}/poison.txt" "must not survive\n")
foreach(path IN ITEMS
        "third_party/skia-android/out/android-arm64/libskia.a"
        "build/_deps/quickjs_ng-src/quickjs.c"
        "build/_deps/lua-src/src/lapi.c"
        "build/_deps/miniaudio-src/miniaudio.h"
        "build/_deps/miniaudio-src/extras/stb_vorbis.c"
        "third_party/stb/stb_image.h")
    get_filename_component(parent "${poison_root}/${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    file(WRITE "${poison_root}/${path}" "poison\n")
endforeach()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "BUDO_ANDROID_SUPPORT_CACHE_DIR=${poison_cache}"
        "BUDO_ANDROID_SUPPORT_TEST_FORCE_PROVISION=1"
        "BUDO_ANDROID_SUPPORT_URL=file://${valid_archive}"
        "BUDO_UNSAFE_ANDROID_SUPPORT_SHA256_OVERRIDE=1"
        "BUDO_ANDROID_SUPPORT_SHA256=${valid_sha}"
        "${BUDO_EXECUTABLE}" android-apk "${fixture_root}/support_archive" --no-build
    RESULT_VARIABLE poison_result OUTPUT_VARIABLE poison_output ERROR_VARIABLE poison_error)
if(NOT poison_result EQUAL 0)
    message(FATAL_ERROR "Verified support archive could not replace poison cache:\n${poison_output}\n${poison_error}")
endif()
if(EXISTS "${poison_root}/poison.txt" OR NOT EXISTS "${poison_root}/source-marker.txt")
    message(FATAL_ERROR "Pre-created marker cache was trusted instead of being replaced")
endif()

set(content_addressed_archive "${fixture_root}/budo-android-support-${valid_sha}.tar.gz")
file(COPY_FILE "${valid_archive}" "${content_addressed_archive}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "BUDO_ANDROID_SUPPORT_CACHE_DIR=${fixture_root}/support-base-url-cache"
        "BUDO_ANDROID_SUPPORT_TEST_FORCE_PROVISION=1"
        "BUDO_BASE_URL=file://${fixture_root}"
        "BUDO_UNSAFE_ANDROID_SUPPORT_SHA256_OVERRIDE=1"
        "BUDO_ANDROID_SUPPORT_SHA256=${valid_sha}"
        "${BUDO_EXECUTABLE}" android-apk "${fixture_root}/support_archive" --no-build
    RESULT_VARIABLE base_url_result OUTPUT_VARIABLE base_url_output ERROR_VARIABLE base_url_error)
if(NOT base_url_result EQUAL 0)
    message(FATAL_ERROR
        "Checksum-addressed support archive could not be resolved from BUDO_BASE_URL:\n"
        "${base_url_output}\n${base_url_error}")
endif()

if(DEFINED PRIVATE_ANDROID_ROOT AND EXISTS "${PRIVATE_ANDROID_ROOT}/scripts/package-android-support.sh")
    set(producer_fixture "${fixture_root}/producer-symlink")
    file(MAKE_DIRECTORY "${producer_fixture}")
    file(WRITE "${producer_fixture}/target.txt" "ordinary file\n")
    file(CREATE_LINK "target.txt" "${producer_fixture}/alias.txt" SYMBOLIC RESULT producer_link_result)
    if(NOT producer_link_result STREQUAL "0")
        message(FATAL_ERROR "Could not create producer symlink fixture: ${producer_link_result}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "BUDO_ANDROID_SUPPORT_VALIDATE_TREE_ONLY=${producer_fixture}"
            "${PRIVATE_ANDROID_ROOT}/scripts/package-android-support.sh"
        RESULT_VARIABLE producer_result OUTPUT_VARIABLE producer_output ERROR_VARIABLE producer_error)
    if(producer_result EQUAL 0 OR
       NOT "${producer_output}\n${producer_error}" MATCHES "must not contain symlinks")
        message(FATAL_ERROR "Android support producer did not reject a symlink:\n${producer_output}\n${producer_error}")
    endif()
endif()
if(EXISTS "${fixture_root}/budo-support-escaped.txt" OR
   EXISTS "${fixture_root}/support-cache-traversal/budo-support-escaped.txt")
    message(FATAL_ERROR "Android support traversal archive escaped extraction")
endif()

file(REMOVE_RECURSE "${fixture_root}")
