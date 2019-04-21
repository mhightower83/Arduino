#!/bin/bash
buildpath="${1}"
runtimeplatformpath="${2}"
version="${3}"

# I changed the original method. We now create a .txt file instead of .h And
# conditionally update the .h if the .txt file changes. This allows the build
# dependencies to work correctly. This avoids a complete rebuild of the project
# every time we compile.
# Added handy #defines for ARDUINO_ESP8266_RELEASE... macro

print_core_version_h() {
cat << /EOF | grep -v "undefined"
#ifndef ARDUINO_ESP8266_GIT_VER
#define ARDUINO_ESP8266_GIT_VER 0x${short_sha1}
#define ARDUINO_ESP8266_GIT_DESC ${git_tag}
#define ARDUINO_ESP8266_RELEASE_${ver_define}
#define ARDUINO_ESP8266_RELEASE "${ver_define_value}"
#endif
/EOF
}

mkdir -p "${buildpath}/core" || exit 1
short_sha1=`git --git-dir "${runtimeplatformpath}/.git" rev-parse --short=8 HEAD 2>/dev/null` || short_sha1="ffffffff"

cd "${runtimeplatformpath}"
if git_tag=`git describe --tags 2>/dev/null`; then
    # Figure out what the package is called
    if plain_ver=`git describe --exact-match 2>/dev/null`; then
        ver_define=`echo "$plain_ver" | tr "[:lower:].\055" "[:upper:]_"`
        ver_define_value="${ver_define}"
    else
        ver_define=`echo "${git_tag%%-*}" | tr "[:lower:].\055" "[:upper:]_"`
        ver_define_value="${ver_define}-dev"
    fi
else
    git_tag="unix-${version}"
    ver_define="undefined"
    ver_define_value="undefined"
fi

print_core_version_h >"${buildpath}/core/core_version.txt"
cmp -s "${buildpath}/core/core_version.txt" "${buildpath}/core/core_version.h" ||
    cp "${buildpath}/core/core_version.txt" "${buildpath}/core/core_version.h"
