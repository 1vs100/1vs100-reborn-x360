# winepath-args.sh — translate Linux paths in XDK-tool args to Wine drive paths.
# Sourced by the cl-wine / link-wine / lib-wine / imagexex-wine wrappers.
# Override the translator in tests with WINEPATH_FN=my_stub.
#
# Path conversion is done in-process (Z: -> /) instead of spawning
# `wine winepath` per argument. Each winepath invocation is a full Wine
# process; ninja-parallel cl.exe already hammers wineserver, and a nested
# winepath at the imagexex step is what stalls the packager "forever".
#
# wineserver / winedevice MUST be started with stdio detached. If the first
# wine process is a ninja compiler step, those services inherit ninja's
# stdout pipe and ninja waits forever for EOF after cl/link/imagexex exits.

ensure_wineserver() {
    export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-xdk}"
    export WINEDEBUG="${WINEDEBUG:--all}"
    export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-winemenubuilder.exe=d}"

    # A global `pgrep wineserver` is wrong: Wine has one server per prefix, so
    # a server for an unrelated prefix says nothing about this build. Identify
    # a server by the WINEPREFIX in its environment instead.
    find_prefix_wineserver() {
        local pid prefix
        for pid in $(pgrep -x wineserver 2>/dev/null || true); do
            [ -r "/proc/$pid/environ" ] || continue
            prefix="$(tr '\0' '\n' < "/proc/$pid/environ" |
                sed -n 's/^WINEPREFIX=//p' | head -n1)"
            if [ "$prefix" = "$WINEPREFIX" ]; then
                printf '%s\n' "$pid"
                return 0
            fi
        done
        return 1
    }

    if find_prefix_wineserver >/dev/null; then
        return 0
    fi

    # Compiler wrappers start in parallel. Serialize first-use initialization
    # so only one of them can create wineserver/winedevice. Every process in
    # this bootstrap has stdio detached before Wine sees it; descendants can
    # therefore never inherit Ninja's edge-output pipe.
    mkdir -p "$WINEPREFIX"
    exec 9>"$WINEPREFIX/.xdk-wineserver.lock"
    flock 9
    if ! find_prefix_wineserver >/dev/null; then
        wineserver -p </dev/null >/dev/null 2>&1 &
        # wineboot is the readiness barrier and causes device services to be
        # born with /dev/null descriptors too. Do not hide initialization
        # failures: a half-created prefix is not safe to compile against.
        wine wineboot --init </dev/null >/dev/null 2>&1
        if ! find_prefix_wineserver >/dev/null; then
            printf 'wine bootstrap failed for prefix %s\n' "$WINEPREFIX" >&2
            flock -u 9
            return 1
        fi
    fi
    flock -u 9
}

# Ninja captures each command's stdout/stderr with a pipe and waits for EOF.
# Wine duplicates inherited handles into long-lived wineserver/winedevice
# processes, so an otherwise finished compiler edge can leave Ninja waiting
# forever.  Never give Wine Ninja's pipe: capture into regular files and replay
# the output from this short-lived wrapper after the Wine client has exited.
run_wine_command() {
    local output_dir stdout_file stderr_file rc
    output_dir="$(mktemp -d "${TMPDIR:-/tmp}/xdk-wine-output.XXXXXX")"
    stdout_file="$output_dir/stdout"
    stderr_file="$output_dir/stderr"

    if "$@" </dev/null >"$stdout_file" 2>"$stderr_file"; then
        rc=0
    else
        rc=$?
    fi

    [ ! -s "$stdout_file" ] || cat "$stdout_file"
    [ ! -s "$stderr_file" ] || cat "$stderr_file" >&2
    rm -f -- "$stdout_file" "$stderr_file"
    rmdir -- "$output_dir"
    return "$rc"
}

to_win_path() {
    if [ -n "${WINEPATH_FN:-}" ]; then "$WINEPATH_FN" "$1"; return; fi

    local unix="$1" prefix zroot rel

    # Already a Windows path.
    case "$unix" in
        [A-Za-z]:[\\/]*) printf '%s' "$unix"; return ;;
    esac

    prefix="${WINEPREFIX:-$HOME/.wine-xdk}"
    zroot="/"
    if [ -L "$prefix/dosdevices/z:" ]; then
        zroot="$(readlink -f "$prefix/dosdevices/z:" 2>/dev/null || echo /)"
        [ -n "$zroot" ] || zroot="/"
    fi

    # Relative -> absolute. realpath -m does not require the path to exist
    # (/OUT: of a file we are about to create).
    if [ "${unix#/}" = "$unix" ]; then
        unix="$(pwd)/$unix"
    fi
    if command -v realpath >/dev/null 2>&1; then
        unix="$(realpath -m "$unix")"
    fi

    rel="${unix#"$zroot"}"
    if [ "$rel" = "$unix" ]; then
        rel="$unix"
    fi
    case "$rel" in
        /*) ;;
        "") rel="/" ;;
        *)  rel="/$rel" ;;
    esac

    printf 'Z:%s' "$(printf '%s' "$rel" | sed 's|/|\\|g')"
}

# Rewrite cl.exe /showIncludes lines ("Note: including file: <spaces>Z:\a\b.h") on stdin to host
# paths (/a/b.h under the Z: drive root); all other lines pass through unchanged. cl prints a path
# as spelled in the #include, which on a case-sensitive host may not exist as such
# (<PPCIntrinsics.h> is ppcintrinsics.h in the XDK), so missing paths are resolved per component
# ignoring case; Ninja would otherwise treat them as missing and rebuild every time.
from_win_includes() {
    local zroot prefix line path lead
    prefix="${WINEPREFIX:-$HOME/.wine-xdk}"
    zroot=""
    if [ -L "$prefix/dosdevices/z:" ]; then
        zroot="$(readlink -f "$prefix/dosdevices/z:" 2>/dev/null || true)"
        [ "$zroot" != "/" ] || zroot=""
    fi
    while IFS= read -r line || [ -n "$line" ]; do
        line="${line%$'\r'}"   # cl.exe under Wine ends lines with CRLF
        case "$line" in
            "Note: including file:"*)
                path="${line#Note: including file:}"
                lead="${path%%[! ]*}"
                path="${path#"$lead"}"
                case "$path" in
                    [Zz]:*)
                        path="${path#??}"
                        path="${zroot}${path//\\//}"
                        [ -e "$path" ] || path="$(host_case_path "$path")"
                        ;;
                esac
                printf 'Note: including file:%s%s\n' "$lead" "$path"
                ;;
            *)
                printf '%s\n' "$line"
                ;;
        esac
    done
}

# Absolute host path with each component matched case-insensitively; unchanged if not found.
host_case_path() {
    local in="$1" out="" comp match
    local -a parts
    IFS='/' read -r -a parts <<< "${in#/}"
    for comp in "${parts[@]}"; do
        if [ -e "$out/$comp" ]; then
            out="$out/$comp"
            continue
        fi
        match="$(find "${out:-/}" -maxdepth 1 -iname "$comp" -print -quit 2>/dev/null)"
        if [ -z "$match" ]; then
            printf '%s' "$in"
            return
        fi
        out="$match"
    done
    printf '%s' "$out"
}

translate_one() {
    local a="$1"
    case "$a" in
        /Fo*|/Fd*|/Fp*|/Fe*) printf '%s' "${a:0:3}"; to_win_path "${a:3}" ;;
        -I*)        printf -- '-I';        to_win_path "${a:2}" ;;
        /OUT:*)     printf '/OUT:';        to_win_path "${a#/OUT:}" ;;
        /def:*)     printf '/def:';        to_win_path "${a#/def:}" ;;
        /IN:*)      printf '/IN:';         to_win_path "${a#/IN:}" ;;
        /CONFIG:*)  printf '/CONFIG:';     to_win_path "${a#/CONFIG:}" ;;
        /PDB:*)     printf '/PDB:';        to_win_path "${a#/PDB:}" ;;
        /IMPLIB:*)  printf '/IMPLIB:';     to_win_path "${a#/IMPLIB:}" ;;
        /LIBPATH:*) printf '/LIBPATH:';    to_win_path "${a#/LIBPATH:}" ;;
        # /I include path: anchor to /I/<abs> or /I.<rel> so link flags that
        # merely start with /I (/INCREMENTAL, /INCLUDE, /IGNORE) are NOT paths.
        /I/*|/I.*)  printf '/I';           to_win_path "${a:2}" ;;
        -*|/*)
            if [ -e "$a" ]; then to_win_path "$a"; else printf '%s' "$a"; fi ;;
        *)
            if [ -e "$a" ] || case "$a" in */*) true ;; *) false ;; esac; then
                to_win_path "$a"
            else
                printf '%s' "$a"
            fi ;;
    esac
}

translate_args() {
    TRANSLATED_ARGS=()
    local a tok rsp tmp
    for a in "$@"; do
        case "$a" in
            @*)
                rsp="${a#@}"; tmp="$(mktemp)"
                while IFS= read -r tok; do
                    [ -n "$tok" ] || continue
                    translate_one "$tok"; printf '\n'
                done < <(xargs -n1 printf '%s\n' < "$rsp") > "$tmp"
                TRANSLATED_ARGS+=( "@$(to_win_path "$tmp")" )
                ;;
            *)
                TRANSLATED_ARGS+=( "$(translate_one "$a")" )
                ;;
        esac
    done
}
