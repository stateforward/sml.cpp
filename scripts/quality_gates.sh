#!/usr/bin/env bash

set -euo pipefail
IFS=$'\n\t'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

usage() {
  cat <<'EOF'
Usage: scripts/quality_gates.sh [options]

Options:
  --help                        Show this help.
  --jobs N                      Parallel job count for builds (default: CPU count).
  --build-dir PATH              Directory used for all quality builds (default: .quality_gates).
  --coverage-min N              Minimum line coverage percentage (default: 90).
  --experimental-sanitizers     Enable optional TSan/MSan stages.
EOF
}

JOBS="${QUALITY_GATE_JOBS:-}"
BUILD_ROOT="${QUALITY_GATE_BUILD_ROOT:-${REPO_ROOT}/.quality_gates}"
COVERAGE_MIN="${QUALITY_GATE_COVERAGE_MIN:-90}"
EXPERIMENTAL_SANITIZERS="${QUALITY_GATE_EXPERIMENTAL_SANITIZERS:-0}"
CMAKE_GENERATOR="${QUALITY_GATE_CMAKE_GENERATOR:-}"
CXX_BIN="${QUALITY_GATE_CXX:-${CXX:-c++}}"
EXTRA_CXX_FLAGS="${QUALITY_GATE_EXTRA_CXX_FLAGS:-}"

require_command() {
  local cmd="$1"
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "Required command '$cmd' is missing." >&2
    exit 1
  fi
}

resolve_tool() {
  local tool_name="$1"
  local brew_tool=""

  if command -v "${tool_name}" >/dev/null 2>&1; then
    echo "${tool_name}"
    return 0
  fi

  if [[ "${OSTYPE}" == "darwin"* ]]; then
    brew_tool="$(brew --prefix llvm 2>/dev/null || true)/bin/${tool_name}"
    if [[ -x "${brew_tool}" ]]; then
      echo "${brew_tool}"
      return 0
    fi
  fi

  return 1
}

resolve_coverage_gcov_tool() {
  local family="$1"
  if [[ "${family}" != "clang" ]]; then
    echo "gcov"
    return 0
  fi

  local llvm_cov=""
  if command -v xcrun >/dev/null 2>&1; then
    llvm_cov="$(xcrun -f llvm-cov 2>/dev/null || true)"
  fi
  if [[ -z "${llvm_cov}" ]]; then
    llvm_cov="$(command -v llvm-cov 2>/dev/null || true)"
  fi

  if [[ -z "${llvm_cov}" ]]; then
    echo "gcov"
    return 0
  fi

  local wrapper_path="${BUILD_ROOT}/coverage/.llvm-gcov-wrapper.sh"
  mkdir -p "${BUILD_ROOT}/coverage"
  printf '#!/usr/bin/env sh\n%s gcov "$@"\n' "${llvm_cov}" > "${wrapper_path}"
  chmod +x "${wrapper_path}"

  echo "${wrapper_path}"
}

cpu_count() {
  if command -v nproc >/dev/null 2>&1; then
    nproc
  elif command -v sysctl >/dev/null 2>&1; then
    sysctl -n hw.nproc 2>/dev/null || echo 4
  else
    echo 4
  fi
}

detect_compiler_family() {
  local version
  if ! version="$("$CXX_BIN" --version 2>/dev/null)"; then
    echo "unknown"
    return
  fi

  if [[ "$CXX_BIN" == "cl" || "$CXX_BIN" == "cl.exe" || "$CXX_BIN" == *"cl.exe"* ]] || [[ "$version" == *"Microsoft"* ]]; then
    echo "msvc"
  elif [[ "$CXX_BIN" == *clang* ]] || [[ "$version" == *"clang"* ]]; then
    echo "clang"
  elif [[ "$CXX_BIN" == *g++* ]] || [[ "$CXX_BIN" == *gcc* ]] || [[ "$version" == *"gcc"* ]]; then
    echo "gcc"
  else
    echo "unknown"
  fi
}

require_defaults() {
  REQUIREMENTS=(
    cmake
    ctest
    find
    git
  )
  for requirement in "${REQUIREMENTS[@]}"; do
    require_command "$requirement"
  done

  CLANG_FORMAT_CMD="$(resolve_tool clang-format)" || {
    echo "Required command 'clang-format' is missing." >&2
    exit 1
  }

  CLANG_TIDY_CMD="$(resolve_tool clang-tidy)" || {
    echo "Required command 'clang-tidy' is missing." >&2
    exit 1
  }

  if ! command -v gcovr >/dev/null 2>&1 && ! command -v lcov >/dev/null 2>&1; then
    echo "Required command 'gcovr' or 'lcov' is missing." >&2
    exit 1
  fi
}

start_section() {
  local section_name="$1"
  echo
  echo "------------------------------------------------------------"
  echo "[${section_name}]"
  echo "------------------------------------------------------------"
}

run_build() {
  local label="$1"
  local cxx_standard="$2"
  local use_exceptions="$3"
  local with_tests="$4"
  local with_examples="$5"
  local cxx_flags="$6"
  shift 6

  local -a extra_args=("$@")
  local build_dir="${BUILD_ROOT}/${label}"

  rm -rf "${build_dir}"
  mkdir -p "${build_dir}"

  local -a cmake_args=(
    -S "${REPO_ROOT}"
    -B "${build_dir}"
    -DCMAKE_BUILD_TYPE=Debug
    -DCMAKE_CXX_COMPILER="${CXX_BIN}"
    -DCMAKE_CXX_STANDARD="${cxx_standard}"
    -DSML_BUILD_TESTS="${with_tests}"
    -DSML_BUILD_EXAMPLES="${with_examples}"
    -DSML_USE_EXCEPTIONS="${use_exceptions}"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    -DCMAKE_CXX_FLAGS="${cxx_flags}"
  )

  if [[ -n "${CMAKE_GENERATOR}" ]]; then
    cmake_args=(-G "${CMAKE_GENERATOR}" "${cmake_args[@]}")
  fi
  if ((${#extra_args[@]} > 0)); then
    cmake_args+=("${extra_args[@]}")
  fi

  cmake "${cmake_args[@]}"
  cmake --build "${build_dir}" -j "${JOBS}"

  if [[ "${with_tests}" == "ON" ]]; then
    ctest --test-dir "${build_dir}" --output-on-failure -j "${JOBS}"
  fi
}

run_format_gate() {
  start_section "format"
  require_command "${CLANG_FORMAT_CMD}"

  local supports_werror=0
  local format_count=0
  local file formatted diff_log
  local -a format_dirs=("${REPO_ROOT}/example" "${REPO_ROOT}/test")
  local -a format_scan_dirs=()
  local scan_dir

  for scan_dir in "${format_dirs[@]}"; do
    if [[ -d "${scan_dir}" ]]; then
      format_scan_dirs+=("${scan_dir}")
    fi
  done

  if (( ${#format_scan_dirs[@]} == 0 )); then
    echo "No source directories found for format check."
    return 1
  fi
  if "${CLANG_FORMAT_CMD}" --help 2>&1 | grep -q -- "--Werror"; then
    supports_werror=1
  fi

  while IFS= read -r -d '' file; do
    format_count=$((format_count + 1))
    if [[ "${supports_werror}" -eq 1 ]]; then
      "${CLANG_FORMAT_CMD}" --style=file --dry-run --Werror "${file}"
    else
        formatted="$(mktemp)"
        diff_log="$(mktemp)"
      "${CLANG_FORMAT_CMD}" --style=file "${file}" > "${formatted}"
      if ! diff -u "${file}" "${formatted}" > "${diff_log}"; then
        rm -f "${formatted}"
        echo "Formatting mismatch: ${file}" >&2
        cat "${diff_log}"
        rm -f "${diff_log}"
        return 1
      fi
      rm -f "${formatted}" "${diff_log}"
    fi
  done < <(
    find "${format_scan_dirs[@]}" \
      \( -name "*.hpp" -o -name "*.cpp" -o -name "*.h" \) -type f -print0 | sort -z
  )

  if ((format_count == 0)); then
    echo "No source files found for format check."
    return 1
  fi

  echo "Format checks passed."
}

run_tidy_gate() {
  start_section "clang-tidy"
  require_command "${CLANG_TIDY_CMD}"

  local strict_flags="${BASE_CXX_FLAGS}"
  local tidy_dir="${BUILD_ROOT}/lint"

  run_build "lint" 20 ON ON OFF "${strict_flags}" -DSML_BUILD_BENCHMARKS=OFF

  local tidy_count=0
  local file
  local tidy_files=()
  local -a tidy_scan_dirs=(
    "${REPO_ROOT}/test/ft"
    "${REPO_ROOT}/test/ut"
    "${REPO_ROOT}/test/unit"
  )
  local tidy_scan_dir
  while IFS= read -r -d '' file; do
    tidy_files+=("${file}")
    tidy_count=$((tidy_count + 1))
  done < <(
    {
      for tidy_scan_dir in "${tidy_scan_dirs[@]}"; do
        if [[ -d "${tidy_scan_dir}" ]]; then
          find "${tidy_scan_dir}" -mindepth 1 -maxdepth 1 -name "*.cpp" -type f -print0
        fi
      done
    } | sort -z
  )

  if ((tidy_count == 0)); then
    echo "No cpp files found for clang-tidy."
    return 1
  fi

  local -a tidy_args=(
    --quiet
    --warnings-as-errors=*
  )

  if [[ "${OSTYPE}" == "darwin"* ]] && command -v xcrun >/dev/null 2>&1; then
    local sdk_path
    sdk_path="$(xcrun --show-sdk-path 2>/dev/null || true)"
    if [[ -n "${sdk_path}" ]]; then
      tidy_args+=(--extra-arg=-isysroot "--extra-arg=${sdk_path}")
    fi
  fi

  "${CLANG_TIDY_CMD}" "${tidy_files[@]}" -p "${tidy_dir}" "${tidy_args[@]}"
  echo "Clang-tidy checks passed."
}

run_regression_matrix() {
  start_section "regression matrix"
  run_build "cxx20" 20 ON ON OFF "${BASE_CXX_FLAGS}"
  run_build "no_exceptions" 20 OFF ON OFF "${BASE_CXX_FLAGS}"
  run_build "cxx14" 14 ON OFF OFF "${BASE_CXX_FLAGS}"
  echo "Regression build/test matrix passed."
}

run_sanitizer_matrix() {
  if [[ "${COMPILER_FAMILY}" == "msvc" ]]; then
    echo "Skipping sanitizer matrix on MSVC."
    return 0
  fi

  start_section "sanitizer matrix"
  local asan_flags="${BASE_CXX_FLAGS} -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-trap=all -fno-sanitize-recover=all"
  run_build "sanitizer_asan_ubsan" 20 ON ON OFF "${asan_flags}" -DSML_BUILD_BENCHMARKS=OFF

  if [[ "${EXPERIMENTAL_SANITIZERS}" -eq 1 ]]; then
    local thread_flags="${BASE_CXX_FLAGS} -fno-omit-frame-pointer -fsanitize=thread -fno-sanitize-trap=all -fno-sanitize-recover=all"
    run_build "sanitizer_thread" 20 ON ON OFF "${thread_flags}" -DSML_BUILD_BENCHMARKS=OFF

    if [[ "${COMPILER_FAMILY}" == "clang" ]]; then
      local mem_flags="${BASE_CXX_FLAGS} -fno-omit-frame-pointer -fsanitize=memory -fsanitize-memory-track-origins=2 -fno-sanitize-recover=all"
      run_build "sanitizer_memory" 20 ON ON OFF "${mem_flags}" -DSML_BUILD_BENCHMARKS=OFF
    fi
  fi

  echo "Sanitizer matrix passed."
}

run_coverage_gate() {
  if [[ "${COMPILER_FAMILY}" == "msvc" ]]; then
    echo "Skipping coverage gate on MSVC."
    return 0
  fi

  start_section "coverage"

  local gcov_tool
  gcov_tool="$(resolve_coverage_gcov_tool "${COMPILER_FAMILY}")"
  if [[ "${COMPILER_FAMILY}" == "clang" && "${gcov_tool}" == "gcov" ]]; then
    echo "Using default gcov for coverage; this may fail on mixed compiler profiles."
  fi

  local coverage_flags="${BASE_CXX_FLAGS} --coverage"
  local coverage_dir="${BUILD_ROOT}/coverage"
  run_build "coverage" 20 ON ON OFF "${coverage_flags}" -DSML_BUILD_BENCHMARKS=OFF

  local coverage_percent=""
  local coverage_report_file="${coverage_dir}/coverage.txt"
  local coverage_report=""
  local used_tool=""
  local lcov_available=0

  if command -v lcov >/dev/null 2>&1; then
    lcov_available=1
  elif command -v gcovr >/dev/null 2>&1; then
    :
  else
    echo "Unable to collect coverage report (lcov or gcovr missing)." >&2
    return 1
  fi

  if [[ "${lcov_available}" -eq 1 ]]; then
    local coverage_info="${coverage_dir}/coverage.info"
    local coverage_extract="${coverage_dir}/coverage-filtered.info"
    local lcov_report="${coverage_dir}/coverage-lcov-summary.txt"
    local lcov_capture_errors="inconsistent,source,format,unsupported,empty,gcov,version"
    local lcov_extract_errors="inconsistent,format,count,source,unsupported"
    local lcov_summary_errors="inconsistent,corrupt,unsupported,count"
    if LC_ALL=C lcov --capture --base-directory "${REPO_ROOT}" --directory "${coverage_dir}" \
      --output-file "${coverage_info}" \
      --gcov-tool "${gcov_tool}" \
      --ignore-errors "${lcov_capture_errors}" \
      >"${coverage_report_file}" 2>&1 && \
      LC_ALL=C lcov --extract "${coverage_info}" "${REPO_ROOT}/*" --output-file "${coverage_extract}" \
      --ignore-errors "${lcov_extract_errors}" \
      >>"${coverage_report_file}" 2>&1 && \
      lcov_summary="$(LC_ALL=C lcov --summary "${coverage_extract}" --ignore-errors "${lcov_summary_errors}" 2>&1)"; then
      echo "${lcov_summary}" > "${lcov_report}"
      used_tool="lcov"
      coverage_report="$(cat "${coverage_report_file}")"
      coverage_report="${coverage_report}"$'\n'"${lcov_summary}"
      coverage_percent="$(printf '%s\n' "${lcov_summary}" | awk '/lines/ {for (i = 1; i <= NF; ++i) { if ($i ~ /%$/) { gsub(/%/, "", $i); print $i; exit } } }')"
    else
      coverage_report="$(cat "${coverage_report_file}")"
      if [[ -f "${lcov_report}" ]]; then
        coverage_report="${coverage_report}"$'\n'"$(cat "${lcov_report}")"
      fi
      echo "${coverage_report}" >&2
    fi
  fi

  if [[ -z "${coverage_percent}" ]] && command -v gcovr >/dev/null 2>&1; then
    if gcovr --root "${REPO_ROOT}" "${coverage_dir}" --txt -j 1 \
      --gcov-executable "${gcov_tool}" --gcov-ignore-errors all \
      >"${coverage_report_file}" 2>&1; then
      used_tool="gcovr"
      coverage_report="$(cat "${coverage_report_file}")"
      coverage_percent="$(printf '%s\n' "${coverage_report}" | awk '/^TOTAL/{for(i=1;i<=NF;i++) if($i ~ /%$/){print $i; exit}}' | head -n 1 | tr -d '%')"
    else
      coverage_report="$(cat "${coverage_report_file}")"
      if [[ "${lcov_available}" -eq 1 ]]; then
        echo "gcovr failed, keeping lcov result if available." >&2
      else
        echo "gcovr failed." >&2
      fi
      echo "${coverage_report}" >&2
    fi
  fi

  if [[ -z "${coverage_percent}" ]]; then
    echo "Unable to collect coverage report." >&2
    echo "${coverage_report}" >&2
    return 1
  fi

  if [[ -z "${used_tool}" ]]; then
    echo "Unknown coverage tool state after report generation." >&2
    return 1
  fi

  echo "${coverage_report}" > "${coverage_report_file}"

  if [[ -z "${coverage_percent}" ]] || [[ "${coverage_percent}" == "--" ]]; then
    echo "Unable to parse coverage percentage." >&2
    echo "${coverage_report}" >&2
    return 1
  fi

  if ! awk -v actual="${coverage_percent}" -v minimum="${COVERAGE_MIN}" 'BEGIN {exit (actual < minimum)}'; then
    echo "Coverage ${coverage_percent}% is below minimum ${COVERAGE_MIN}%." >&2
    return 1
  fi

  echo "Coverage gate passed with ${coverage_percent}% (minimum ${COVERAGE_MIN}%) using ${used_tool}."
}

run_benchmarks_gate() {
  start_section "benchmarks"
  run_build "benchmarks" 17 ON OFF OFF "${BASE_CXX_FLAGS}" -DSML_BUILD_BENCHMARKS=ON -DSML_BUILD_TESTS=OFF -DSML_BUILD_EXAMPLES=OFF
  echo "Benchmark smoke build passed."
}

parse_args() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --help)
        usage
        exit 0
        ;;
      --jobs)
        shift
        JOBS="$1"
        ;;
      --build-dir)
        shift
        BUILD_ROOT="$1"
        ;;
      --coverage-min)
        shift
        COVERAGE_MIN="$1"
        ;;
      --experimental-sanitizers)
        EXPERIMENTAL_SANITIZERS=1
        ;;
      *)
        echo "Unknown option: $1" >&2
        usage
        exit 1
        ;;
    esac
    shift
  done
}

parse_args "$@"

if [[ -z "${JOBS}" ]]; then
  JOBS="$(cpu_count)"
fi
mkdir -p "${BUILD_ROOT}"

COMPILER_FAMILY="$(detect_compiler_family)"
if [[ "${COMPILER_FAMILY}" == "gcc" || "${COMPILER_FAMILY}" == "clang" ]]; then
  BASE_CXX_FLAGS="-Wall -Wextra -Werror -pedantic -pedantic-errors"
elif [[ "${COMPILER_FAMILY}" == "msvc" ]]; then
  BASE_CXX_FLAGS="/W4 /WX /permissive-"
else
  BASE_CXX_FLAGS="-Wall -Wextra -Werror -pedantic -pedantic-errors"
fi
if [[ -n "${EXTRA_CXX_FLAGS}" ]]; then
  BASE_CXX_FLAGS="${BASE_CXX_FLAGS} ${EXTRA_CXX_FLAGS}"
fi

require_defaults

start_section "summary"
echo "repo      : ${REPO_ROOT}"
echo "build dir : ${BUILD_ROOT}"
echo "jobs      : ${JOBS}"
echo "cxx       : ${CXX_BIN}"
echo "compiler  : ${COMPILER_FAMILY}"
echo "coverage  : ${COVERAGE_MIN}%"
echo "lint      : enabled"
echo "sanitizers: enabled"
echo "coverage gate: enabled"
echo "benchmarks: enabled"
if [[ "${EXPERIMENTAL_SANITIZERS}" -eq 1 ]]; then
  echo "experimental sanitizers: enabled"
fi
echo

run_format_gate
run_tidy_gate
run_regression_matrix
run_sanitizer_matrix
run_coverage_gate
run_benchmarks_gate

echo
echo "All quality gates passed."
