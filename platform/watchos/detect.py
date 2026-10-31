import os
import sys
from typing import TYPE_CHECKING

from methods import detect_darwin_sdk_path, detect_darwin_toolchain_path, print_error, print_warning
from platform_methods import validate_arch

if TYPE_CHECKING:
    from SCons.Script.SConscript import SConsEnvironment

# Minimum watchOS version, needed for the SwiftUI lifecycle and the Observation framework.
WATCHOS_MIN_VERSION = "10.0"


def get_name():
    return "watchOS"


def can_build():
    if sys.platform == "darwin" or ("OSXCROSS_WATCHOS" in os.environ):
        return True

    return False


def get_opts():
    from SCons.Variables import BoolVariable

    return [
        (("SWIFT_COMPILER", "SWIFT_FRONTEND"), "Path to the swiftc binary", ""),
        # APPLE_TOOLCHAIN_PATH Example: /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain
        ("APPLE_TOOLCHAIN_PATH", "Path to the Apple toolchain", ""),
        (("APPLE_SDK_PATH", "WATCHOS_SDK_PATH"), "Path to the watchOS SDK", ""),
        ("apple_target_triple", "Triple for corresponding target Apple platform toolchain", ""),
        BoolVariable("simulator", "Build for Simulator", False),
        BoolVariable("generate_bundle", "Generate an APP bundle after building watchOS/macOS binaries", False),
    ]


def get_doc_classes():
    return [
        "EditorExportPlatformWatchOS",
    ]


def get_doc_path():
    return "doc_classes"


def get_flags():
    return {
        "arch": "arm64",
        "target": "template_debug",
        # watchOS has no GPU API that Godot can use (no Metal, OpenGL ES or Vulkan).
        "use_volk": False,
        "metal": False,
        "vulkan": False,
        "opengl3": False,
        "supported": [],
        "builtin_pcre2_with_jit": False,
        "sdl": False,
        "accesskit": False,
    }


def configure(env: "SConsEnvironment"):
    # Validate arch.
    # arm64_32 (64-bit instructions with 32-bit pointers) is used by older Apple Watch models.
    supported_arches = ["arm64", "arm64_32"]
    validate_arch(env["arch"], get_name(), supported_arches)
    if env["arch"] == "arm64_32" and env["simulator"]:
        print_error("The watchOS simulator doesn't support arm64_32, use arch=arm64.")
        sys.exit(255)
    detect_darwin_toolchain_path(env)

    ## LTO

    if env["lto"] == "auto":  # Disable by default as it makes linking in Xcode very slow.
        env["lto"] = "none"

    if env["lto"] != "none":
        if env["lto"] == "thin":
            env.Append(CCFLAGS=["-flto=thin"])
            env.Append(LINKFLAGS=["-flto=thin"])
        else:
            env.Append(CCFLAGS=["-flto"])
            env.Append(LINKFLAGS=["-flto"])

    ## Compiler configuration

    # Save this in environment for use by other modules
    if "OSXCROSS_WATCHOS" in os.environ:
        env["osxcross"] = True

    env["ENV"]["PATH"] = env["APPLE_TOOLCHAIN_PATH"] + "/Developer/usr/bin/:" + env["ENV"]["PATH"]

    compiler_path = "$APPLE_TOOLCHAIN_PATH/usr/bin/${apple_target_triple}"

    ccache_path = os.environ.get("CCACHE")
    if ccache_path is None:
        env["CC"] = compiler_path + "clang"
        env["CXX"] = compiler_path + "clang++"
        env["S_compiler"] = compiler_path + "clang"
    else:
        # there aren't any ccache wrappers available for watchOS,
        # to enable caching we need to prepend the path to the ccache binary
        env["CC"] = ccache_path + " " + compiler_path + "clang"
        env["CXX"] = ccache_path + " " + compiler_path + "clang++"
        env["S_compiler"] = ccache_path + " " + compiler_path + "clang"
    env["AR"] = compiler_path + "ar"
    env["RANLIB"] = compiler_path + "ranlib"

    ## Compile flags

    if env["simulator"]:
        env["APPLE_PLATFORM"] = "watchossimulator"
        env.Append(ASFLAGS=[f"-mtargetos=watchos{WATCHOS_MIN_VERSION}-simulator"])
        env.Append(CCFLAGS=[f"-mtargetos=watchos{WATCHOS_MIN_VERSION}-simulator"])
        env.Append(CPPDEFINES=["WATCHOS_SIMULATOR"])
        env.extra_suffix = ".simulator" + env.extra_suffix
    else:
        env["APPLE_PLATFORM"] = "watchos"
        env.Append(ASFLAGS=[f"-mtargetos=watchos{WATCHOS_MIN_VERSION}"])
        env.Append(CCFLAGS=[f"-mtargetos=watchos{WATCHOS_MIN_VERSION}"])
    detect_darwin_sdk_path(env["APPLE_PLATFORM"], env)

    env.Append(CCFLAGS=["-ffp-contract=off"])

    if env["arch"] in ["arm64", "arm64_32"]:
        env.Append(
            CCFLAGS=(
                f"-fobjc-arc -arch {env['arch']} -fmessage-length=0"
                " -fdiagnostics-print-source-range-info -fdiagnostics-show-category=id -fdiagnostics-parseable-fixits"
                " -fpascal-strings -fblocks -fvisibility=hidden -MMD -MT dependencies"
                " -isysroot $APPLE_SDK_PATH".split()
            )
        )
        env.Append(ASFLAGS=["-arch", env["arch"]])

    env.Prepend(CPPPATH=["$APPLE_SDK_PATH/usr/include"])

    env.Prepend(CPPPATH=["#platform/watchos"])
    env.Append(
        CPPDEFINES=[
            "WATCHOS_ENABLED",
            "UNIX_ENABLED",
            "SOFTWARE_RENDERER_ENABLED",
        ]
    )

    if env["vulkan"]:
        print_warning("The watchOS platform does not support the Vulkan rendering driver")
        env["vulkan"] = False

    if env["metal"]:
        print_warning("The watchOS platform does not support the Metal rendering driver")
        env["metal"] = False

    if env["opengl3"]:
        print_warning("The watchOS platform does not support the OpenGL rendering driver")
        env["opengl3"] = False
