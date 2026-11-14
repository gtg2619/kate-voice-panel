#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${KATE_VOICE_BUILD_DIR:-$project_dir/build}"
sdk_dir="${KATE_VOICE_SDK:-/tmp/kate-agent-sdk}"
test_node="${KATE_VOICE_TEST_NODE:-$(command -v node)}"
"$test_node" -e 'const [major,minor]=process.versions.node.split(".").map(Number); if(major<24 || major===24 && minor<5) {console.error("Node.js 24.5+ is required.");process.exit(1)}'
args=(-S "$project_dir" -B "$build_dir" -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug -DNODE_EXECUTABLE="$test_node")
if [[ -f "$sdk_dir/usr/lib64/libQt6WebEngineCore.so.6" ]]; then
    args+=(-DCMAKE_PREFIX_PATH="$sdk_dir/usr")
    export LD_LIBRARY_PATH="$sdk_dir/usr/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export QTWEBENGINEPROCESS_PATH="$sdk_dir/usr/lib64/qt6/libexec/QtWebEngineProcess"
    export QTWEBENGINE_RESOURCES_PATH="$sdk_dir/usr/share/qt6/resources"
    export QTWEBENGINE_LOCALES_PATH="$sdk_dir/usr/share/qt6/translations/qtwebengine_locales"
fi
cmake "${args[@]}"
cmake --build "$build_dir" --parallel "${KATE_VOICE_BUILD_JOBS:-4}"
export QTWEBENGINE_CHROMIUM_FLAGS="${QTWEBENGINE_CHROMIUM_FLAGS:---disable-gpu}"
run=()
if command -v xvfb-run >/dev/null; then run=(xvfb-run -a env QT_QPA_PLATFORM=xcb); fi
"${run[@]}" ctest --test-dir "$build_dir" --output-on-failure --output-junit "$build_dir/test-results.xml"
