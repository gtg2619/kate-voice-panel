#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${KATE_VOICE_BUILD_DIR:-$project_dir/build}"
sdk_dir="${KATE_VOICE_SDK:-/tmp/kate-agent-sdk}"

# A normal system installation needs no SDK variables. The temporary SDK is useful
# on the development machine where root access is unavailable; no packages are installed.
if [[ -f "$sdk_dir/usr/lib64/libQt6WebEngineCore.so.6" ]]; then
    export LD_LIBRARY_PATH="$sdk_dir/usr/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export QTWEBENGINEPROCESS_PATH="$sdk_dir/usr/lib64/qt6/libexec/QtWebEngineProcess"
    export QTWEBENGINE_RESOURCES_PATH="$sdk_dir/usr/share/qt6/resources"
    export QTWEBENGINE_LOCALES_PATH="$sdk_dir/usr/share/qt6/translations/qtwebengine_locales"
fi
if [[ ! -f "$build_dir/bin/kf6/ktexteditor/katevoicepanel.so" ]]; then
    echo "Build the plugin first; see README.md." >&2
    exit 1
fi
export QT_PLUGIN_PATH="$build_dir/bin${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
exec kate --new --startanon "$@"
